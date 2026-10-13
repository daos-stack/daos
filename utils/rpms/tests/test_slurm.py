"""Exercise Slurm packaging through the shared FPM helpers without building RPMs."""

import ast
import configparser
from contextlib import ExitStack
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[3]


class SlurmPackagingTest(unittest.TestCase):
    """Check FPM metadata and staged payloads for the Slurm dependency."""

    def setUp(self):
        workspace = ExitStack()
        self.addCleanup(workspace.close)
        self.root = Path(workspace.enter_context(tempfile.TemporaryDirectory()))
        self.prefix = self.root / "slurm"
        self.tools = self.root / "tools"
        self.tools.mkdir()
        (self.root / "utils").symlink_to(REPO_ROOT / "utils", target_is_directory=True)
        (self.root / ".build_vars.sh").write_text(
            f'export SL_PREFIX="{self.root}"\n'
            f'export SL_SLURM_PREFIX="{self.prefix}"\n'
            'export SL_SRC_DIR=""\n', encoding="utf-8")
        payloads = {
            "bin/srun": "client",
            "bin/sbatch": "client",
            "lib64/libslurm.so.44": "library",
            "lib64/slurm/auth_munge.so": "plugin",
            "lib64/slurm/accounting_storage_mysql.so": "accounting plugin",
            "lib64/slurm/auth_none.so": "excluded",
            "lib64/slurm/cred_none.so": "excluded",
            "lib64/slurm/plugin.la": "excluded",
            "lib64/slurm/src/internal.so": "internal library",
            "lib64/pkgconfig/slurm.pc": "prefix=/usr\nlibdir=/usr/lib64\n",
            "include/slurm/slurm.h": "header",
            "etc/slurm/slurm.conf.example": "example",
            "etc/slurm/cgroup.conf.example": "example",
            "etc/slurm/slurmdbd.conf.example": "database example",
        }
        for section in (1, 5, 8):
            payloads[f"share/man/man{section}/slurm.{section}"] = "manual"
        for daemon in ("slurmctld", "slurmd", "slurmstepd", "slurmdbd"):
            payloads[f"sbin/{daemon}"] = "daemon"
        for daemon in ("slurmctld", "slurmd", "slurmdbd"):
            payloads[f"lib/systemd/system/{daemon}.service"] = (
                f"[Service]\nExecStart=/usr/sbin/{daemon} -D\n")
        for relative, contents in payloads.items():
            target = self.prefix / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(contents, encoding="utf-8")
        (self.prefix / "lib64/libslurm.so").symlink_to("libslurm.so.44")
        fpm = self.tools / "fpm"
        fpm.write_text(
            "#!/usr/bin/env python3\n"
            "import json, os, pathlib, sys\n"
            "args = sys.argv[1:]\n"
            "payload = {}\n"
            "hooks = {}\n"
            "for arg in args:\n"
            "    if '=' in arg and arg.startswith(os.environ['TMPDIR']):\n"
            "        source, target = arg.split('=', 1)\n"
            "        path = pathlib.Path(source)\n"
            "        payload[target] = path.read_text()\n"
            "for index, arg in enumerate(args):\n"
            "    if arg in ('--after-install', '--before-remove', '--after-remove'):\n"
            "        hooks[arg] = pathlib.Path(args[index + 1]).read_text()\n"
            "with open(os.environ['FPM_LOG'], 'a') as stream:\n"
            "    stream.write(json.dumps({'args': args, 'payload': payload, 'hooks': hooks})"
            " + '\\n')\n",
            encoding="utf-8")
        fpm.chmod(0o755)
        patchelf = self.tools / "patchelf"
        patchelf.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
        patchelf.chmod(0o755)
        self.log = self.root / "fpm.jsonl"

    def package(self, distro="el9", output_type="rpm"):
        """Run the real wrapper and helpers against an installed-tree fixture."""
        environment = dict(os.environ, PATH=f"{self.tools}:{os.environ['PATH']}",
                           TMPDIR=str(self.root), FPM_LOG=str(self.log), DISTRO=distro,
                           OUTPUT_TYPE=output_type, PACKAGE_OUTPUT_DIR=str(self.root / "rpms"))
        return subprocess.run(["bash", str(REPO_ROOT / "utils/rpms/slurm.sh")],
                              cwd=self.root, env=environment, capture_output=True, text=True,
                              check=False)

    def test_package_payloads_and_dependencies(self):
        """Packages have disjoint payloads and matching runtime requirements."""
        for distro in ("el9", "suse.lp156"):
            with self.subTest(distro=distro):
                self.log.unlink(missing_ok=True)
                result = self.package(distro)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                packages = {}
                all_paths = set()
                for line in self.log.read_text(encoding="utf-8").splitlines():
                    package = json.loads(line)
                    args = package["args"]
                    name = args[args.index("--name") + 1]
                    packages[name] = package
                    paths = set(package["payload"])
                    self.assertTrue(paths)
                    self.assertFalse(all_paths & paths)
                    all_paths.update(paths)
                self.assertEqual(set(packages), {"slurm", "slurm-slurmctld", "slurm-slurmd",
                                                 "slurm-slurmdbd", "slurm-devel",
                                                 "slurm-example-configs"})
                self.assertIn("/usr/lib64/slurm/src/internal.so", all_paths)
                for excluded in ("auth_none.so", "cred_none.so", "plugin.la"):
                    self.assertFalse(any(path.endswith(excluded) for path in all_paths))
                for name in ("slurm-devel", "slurm-slurmctld", "slurm-slurmd", "slurm-slurmdbd"):
                    self.assertIn(f"slurm = 26.05.4-2.{distro}", packages[name]["args"])
                self.assertIn("/usr/sbin/slurmstepd", packages["slurm-slurmd"]["payload"])
                self.assertIn("/usr/lib64/slurm/accounting_storage_mysql.so",
                              packages["slurm-slurmdbd"]["payload"])
                for daemon in ("slurmctld", "slurmd", "slurmdbd"):
                    package = packages[f"slurm-{daemon}"]
                    unit = package["payload"][f"/usr/lib/systemd/system/{daemon}.service"]
                    self.assertIn(f"ExecStart=/usr/sbin/{daemon}", unit)
                    self.assertIn('"$1" = 0', package["hooks"]["--before-remove"])
                    self.assertIn("daemon-reload", package["hooks"]["--after-install"])
                examples = packages["slurm-example-configs"]["args"]
                self.assertEqual(examples[examples.index("--architecture") + 1], "noarch")
                self.assertEqual(examples.count("--config-files"), 3)

    def test_missing_required_files_fail(self):
        """Do not emit an incomplete compute-node package."""
        (self.prefix / "sbin/slurmstepd").unlink()
        self.assertNotEqual(self.package().returncode, 0)

    def test_non_rpm_output_rejected(self):
        """The standalone wrapper cannot silently generate unsupported packages."""
        result = self.package(output_type="deb")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("RPM only", result.stderr)

    def test_missing_accounting_plugin_fails(self):
        """Reject a source build without the database daemon's accounting backend."""
        (self.prefix / "lib64/slurm/accounting_storage_mysql.so").unlink()
        result = self.package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("accounting_storage_mysql.so", result.stderr)

    def test_dependency_installer_requests_database_rpm(self):
        """Install the pinned database-daemon RPM without touching the host system."""
        install_log = self.root / "install.log"
        sudo = self.tools / "sudo"
        sudo.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$INSTALL_LOG"\n',
                        encoding="utf-8")
        sudo.chmod(0o755)
        identity = self.tools / "id"
        identity.write_text('#!/bin/sh\nprintf "1000\\n"\n', encoding="utf-8")
        identity.chmod(0o755)
        environment = dict(os.environ, PATH=f"{self.tools}:{os.environ['PATH']}",
                           INSTALL_LOG=str(install_log), DAOS_DEPS_EXT_REPO="")
        result = subprocess.run(["bash", str(REPO_ROOT / "utils/build/install_deps.sh"), "el9"],
                                cwd=self.root, env=environment, capture_output=True, text=True,
                                check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("dnf install --allowerasing -y slurm-slurmdbd-26.05.4-2.el9",
                      install_log.read_text(encoding="utf-8").splitlines())

    def test_source_definition_and_version_match(self):
        """The registered source build targets final RPM paths and a staging prefix."""
        config = configparser.ConfigParser()
        config.read(REPO_ROOT / "utils/build.config")
        source = (REPO_ROOT / "site_scons/components/__init__.py").read_text(encoding="utf-8")
        definitions = [node for node in ast.walk(ast.parse(source))
                       if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                       and node.func.attr == "define" and node.args
                       and isinstance(node.args[0], ast.Constant)
                       and node.args[0].value == "slurm"]
        self.assertEqual(len(definitions), 1)
        keywords = {keyword.arg: keyword.value for keyword in definitions[0].keywords}
        commands = ast.literal_eval(keywords["commands"])
        self.assertIn("--prefix=/usr", commands[0])
        self.assertIn("--sysconfdir=/etc/slurm", commands[0])
        self.assertIn("--enable-pkgconfig", commands[0])
        self.assertIn("--with-mysql_config=/usr/bin", commands[0])
        self.assertNotIn("--without-mysql", commands[0])
        self.assertEqual(ast.literal_eval(keywords["required_progs"]), ["mysql_config"])
        self.assertIn(
            ["install", "-D", "-m", "600", "etc/slurmdbd.conf.example",
             "$SLURM_PREFIX/etc/slurm/slurmdbd.conf.example"], commands)
        self.assertIn(["make", "install", "DESTDIR=$SLURM_PREFIX"], commands)
        self.assertIn(["cp", "-a", "$SLURM_PREFIX/usr/.", "$SLURM_PREFIX/"], commands)
        self.assertEqual(ast.literal_eval(keywords["requires"]), ["munge", "hwloc"])
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stderr)
        runtime = json.loads(self.log.read_text(encoding="utf-8").splitlines()[0])
        args = runtime["args"]
        version = args[args.index("--version") + 1]
        self.assertEqual(config["commit_versions"]["slurm"],
                         "slurm-" + version.replace(".", "-") + "-1")


if __name__ == "__main__":
    unittest.main()
