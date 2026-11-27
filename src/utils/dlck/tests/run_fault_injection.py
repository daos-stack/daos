#!/usr/bin/env python3
"""
Copyright 2026 Hewlett Packard Enterprise Development LP
All rights reserved.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import difflib
import json
import os
import re
import subprocess  # nosec
import tempfile
from enum import Enum
from pathlib import Path

import yaml

# defaults and paths
DEFAULT_DLCK = "/opt/daos/bin/dlck"
DEFAULT_POOL_UUID = "00000000-1111-2222-3333-444444444444"
TESTS_DIR = Path(__file__).resolve().parent
TEMPLATE_CONFIG = TESTS_DIR / "fault_injection_dlck.yaml"
TEMPLATE_DIR = TESTS_DIR / "fault_templates"
BUILD_DIR = Path(json.loads((TESTS_DIR.parents[3] / ".build_vars.json").read_text())["BUILD_DIR"])
NVME_FAULTS = ["DLCK_FAULT_POOL_OPEN_BIO", "DLCK_FAULT_POOL_NVME_HEALTH"]


# usage and arguments
USAGE = f"""Run dlck with one fault from fault_injection_dlck.yaml enabled and compare the results \
against the template.

FAULT is a fault name (e.g. DLCK_FAULT_GETGRNAM) or its id (e.g. 131329).
Without FAULT, every fault in the template is run.
Expected results live in fault_templates/FAULT/: output.txt and the log files next to it.
Template placeholders:
  {{UUID}}      any UUID (e.g. the pool UUID)
  {{STORAGE}}   storage path (e.g. /dev/shm/daos or /mnt/daos)
  {{LOG_DIR}}   dlck temporary log directory

MD-on-PMem mode:
  In this mode, a pool is automatically created and populated using `dlck_test_helper`. \
Most templates are optimized for this mode, but some faults cannot be tested in it.

MD-on-SSD mode:
  Some faults can be tested only in this mode: {', '.join(NVME_FAULTS)}. Sadly, pool in this mode \
cannot be created automatically. So, you have to create a DAOS setup in the MD-on-SSD mode and \
create a pool manually. Having that you can test these faults as shown below. \
Note most of the templates are optimized for the MD-on-PMem mode so running faults other than list \
below in the MD-on-SSD mode may report false negatives.

{Path(__file__).name} --storage <STORAGE> --nvme <NVME_DEVICE> --pool_uuid <POOL_UUID> \
{' '.join(NVME_FAULTS)}
"""  # noqa: E501 line too long
PARSER = argparse.ArgumentParser(description=USAGE,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
PARSER.add_argument("--dlck", default=str(DEFAULT_DLCK), help="path to the dlck binary")
PARSER.add_argument("--storage", default="/mnt/daos", help="dlck --storage argument")
PARSER.add_argument("--pool_uuid", default=DEFAULT_POOL_UUID, help="Pool UUID")
PARSER.add_argument("--nvme", default=None, help="dlck --nvme argument. "
                    "dlck_test_helper cannot create a pool in the MD-on-SSD mode "
                    "so the pool must be created manually.")
PARSER.add_argument("--update", action="store_true",
                    help="store the results of this run as the template")
PARSER.add_argument("faults", nargs="*", metavar="FAULT",
                    help="fault name or id (default: all faults in the template)")


class FaultCatalog:
    """Reads the faults listed in the template config."""

    # these faults have a very specific requirements which are not supported by this script
    UNSUPPORTED_FAULTS = [
        "DLCK_MOCK_NOT_IN_DAOS_SERVER_GROUP",
        "DLCK_MOCK_NO_DAOS_SERVER_GROUP",
        "DLCK_FAULT_GETGROUPS",
        "DLCK_FAULT_GETGRNAM"]

    DISABLED_FAULTS = [
        "DAOS_FAULT_DAE_ALLOC"  # DAOS-19969
    ]

    # '  # - id: 131333 # DLCK_FAULT_CREATE_LOG_DIR'
    _FAULT_RE = re.compile(r"^\s*#?\s*-\s*id:\s*(\d+)\s*#\s*(\w+)")

    def __init__(self, config_path):
        self._config_path = config_path
        self._faults = self._read_faults()

    @property
    def faults(self):
        """Return the complete dictionary of faults (id, name) in the catalog."""
        return self._faults

    def _read_faults(self):
        """Return the (id, name) of each fault in the template config."""
        faults = []
        for line in self._config_path.read_text().splitlines():
            match = self._FAULT_RE.match(line)
            if match:
                faults.append((match.group(1), match.group(2)))
        if not faults:
            raise SystemExit(f"No faults found in {self._config_path}")
        return faults

    def find(self, key):
        """Return the (id, name) entry matching a fault name or id."""
        for fault in self._faults:
            if key in fault:
                return fault
        raise SystemExit(f"fault '{key}' not found in {self._config_path}")


class DlckTestHelperRunner:
    """Runs dlck_test_helper under sudo."""

    DLCK_TEST_HELPER = BUILD_DIR / "src/utils/dlck/tests/dlck_test_helper"

    def __init__(self, storage, pool_uuid):
        self._storage = storage
        self._pool_uuid = pool_uuid
        self._files_created = False

    def _cleanup_paths(self):
        """Return the VOS file and system database paths under the storage path."""
        return [Path(self._storage) / self._pool_uuid / "vos-0", Path(self._storage) / "sys_db"]

    def init(self):
        """Run dlck_test_helper"""
        existing = [p for p in self._cleanup_paths() if p.exists()]
        if existing:
            names = ", ".join(str(p) for p in existing)
            raise SystemExit(f"stale files already exist, remove them first: {names}")
        cmd = ["sudo", "-E", str(self.DLCK_TEST_HELPER), "--file", self._pool_uuid,
               "--storage", self._storage, "--create_pools", "--verbose", "--targets", "1"]
        env = dict(os.environ, DLCK_NO_NVME="1")
        proc = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, check=False)
        if proc.returncode != 0:
            raise SystemExit(f"dlck_test_helper failed with exit code {proc.returncode}\n\n"
                             f"{proc.stderr}\n\n{proc.stdout}")
        print(proc.stdout)
        self._files_created = True
        # Change permissions of the VOS file to be read-only.
        vos_file = Path(self._storage) / self._pool_uuid / "vos-0"
        subprocess.run(["sudo", "chmod", "440", str(vos_file)], check=True)

    def fini(self):
        """Remove created VOS file and system database."""
        if not self._files_created:
            return
        subprocess.run(["sudo", "rm", "-f", "--", *map(str, self._cleanup_paths())], check=True)
        self._files_created = False


class DlckRunner:
    """Runs dlck under sudo."""

    def __init__(self, dlck, file_arg, storage, nvme=None):
        self._dlck = dlck
        self._file_arg = file_arg
        self._storage = storage
        self._nvme = nvme

    def has_nvme(self):
        """Return True if an NVMe device is specified."""
        return self._nvme is not None

    def run(self, config, env={}):
        """Run dlck with the fault config; return the exit code and stdout."""
        env = dict(os.environ, D_FI_CONFIG=str(config), **env)
        cmd = ["sudo", "-E", str(self._dlck), "--file", self._file_arg,
               "--storage", self._storage, "--targets", "1"]
        if self._nvme is not None:
            cmd += ["--nvme", self._nvme]
        else:
            env["DLCK_NO_NVME"] = "1"
        proc = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, check=False)
        return proc.returncode, proc.stdout, proc.stderr


class TemplateMatcher:
    """Matches actual dlck output against template text with placeholders."""

    LOG_DIR_RE = "/tmp/dlck_check_[A-Za-z0-9]{6}"
    STORAGE_RE = r"/[\w./-]+/daos(?=/)"
    _UUID_RE = "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}"

    @classmethod
    def normalize(cls, text):
        """Replace the variable parts of captured text with template placeholders."""
        text = re.sub(cls._UUID_RE, "{{UUID}}", text)
        text = re.sub(cls.STORAGE_RE, "{{STORAGE}}", text)
        return re.sub(cls.LOG_DIR_RE, "{{LOG_DIR}}", text)

    @classmethod
    def compare_text(cls, name, expected, actual):
        """Return a unified diff between a template and the actual text, empty if they match."""
        exp_lines = cls.normalize(expected).splitlines()
        act_lines = cls.normalize(actual).splitlines()
        return list(difflib.unified_diff(exp_lines, act_lines, fromfile=f"template/{name}",
                                         tofile=f"actual/{name}", lineterm=""))

    @classmethod
    def compare_logs(cls, expected_files, actual_dir):
        """Match log files by name and compare their contents."""
        expected = {p.name: p for p in expected_files}
        actual = ({cls.normalize(p.name): p for p in actual_dir.iterdir()}
                  if actual_dir.is_dir() else {})
        errors = []
        for name, path in sorted(expected.items()):
            if name not in actual:
                errors.append(f"missing log file '{name}'")
                continue
            errors += cls.compare_text(f"logs/{name}", path.read_text(),
                                       actual.pop(name).read_text())
        errors += [f"unexpected log file '{name}'" for name in sorted(actual)]
        return errors


class FaultTemplate:
    """Expected output and logs of one fault, stored in a template directory."""

    def __init__(self, path):
        self._path = path
        self._output_file = path / "output.txt"

    def exists(self):
        """Does the given template exist?"""
        return self._output_file.is_file()

    def _log_files(self):
        """Return the log files stored next to the output file."""
        if not self._path.is_dir():
            return []
        return [p for p in self._path.iterdir() if p.is_file() and p != self._output_file]

    def update(self, output, logs_dir):
        """Store the normalized output and log files as the template."""
        self._path.mkdir(parents=True, exist_ok=True)
        for old_log in self._log_files():
            old_log.unlink()
        self._output_file.write_text(TemplateMatcher.normalize(output))
        if logs_dir.is_dir():
            for log in logs_dir.iterdir():
                name = TemplateMatcher.normalize(log.name)
                (self._path / name).write_text(TemplateMatcher.normalize(log.read_text()))

    def compare(self, output, logs_dir):
        """Return mismatch messages between this template and the actual output and logs."""
        errors = TemplateMatcher.compare_text("output.txt", self._output_file.read_text(), output)
        errors += TemplateMatcher.compare_logs(self._log_files(), logs_dir)
        return errors


class FaultInjectionTest:
    """Runs faults through dlck and compares or stores the results."""

    class ReturnCode(Enum):
        """Return codes for the fault injection test."""
        OK = 0
        FAIL = 1
        NO_TEMPLATE = 2
        DISABLED = 3
        NOT_SUPPORTED = 4
        NVME_REQUIRED = 5

    @staticmethod
    def rc_to_str_with_padding(code):
        """Return the display text of a ReturnCode."""
        labels = {
            FaultInjectionTest.ReturnCode.OK: "OK\t\t",
            FaultInjectionTest.ReturnCode.FAIL: "FAIL\t\t",
            FaultInjectionTest.ReturnCode.NO_TEMPLATE: "NO TEMPLATE\t",
            FaultInjectionTest.ReturnCode.DISABLED: "DISABLED\t",
            FaultInjectionTest.ReturnCode.NOT_SUPPORTED: "NOT SUPPORTED\t",
            FaultInjectionTest.ReturnCode.NVME_REQUIRED: "NVME REQUIRED\t"
        }
        return labels[code]

    def __init__(self, catalog, runner, templates_dir, update):
        self._catalog = catalog
        self._runner = runner
        self._templates_dir = templates_dir
        self._update = update

    @staticmethod
    def _make_config(fault_id):
        """Return the YAML config text that enables only the given fault."""
        return yaml.safe_dump({"fault_config": [{"id": int(fault_id)}]}, sort_keys=False)

    @staticmethod
    def _env(name):
        """Return the environment variables for the given fault."""
        pool_faults = ["ENGINE", "POOL", "CONT_TREE"]
        cont_faults = ["CONT", "OBJ_ILOG", "DAE", "DBD"]
        env_check_pool_only = {
            "DLCK_CHECK_NO_CONTAINERS": "1",
            "DLCK_CHECK_NO_OBJECTS": "1",
        }
        env_check_cont_only = {
            "DLCK_CHECK_NO_POOLS": "1",
            "DLCK_CHECK_NO_OBJECTS": "1",
        }
        if any(fault in name for fault in pool_faults):
            return env_check_pool_only
        elif any(fault in name for fault in cont_faults):
            return env_check_cont_only
        return {}

    @staticmethod
    def _collect_logs(log_dir, dest):
        """Copy dlck's log directory to dest and remove the original."""
        subprocess.run(["sudo", "cp", "-r", log_dir, str(dest)], check=True)
        subprocess.run(["sudo", "chown", "-R", f"{os.getuid()}:{os.getgid()}",
                        str(dest)], check=True)
        subprocess.run(["sudo", "rm", "-rf", log_dir], check=True)

    def _skip_code(self, name):
        """Return the ReturnCode for a fault that must be skipped, or None to run it."""
        if name in FaultCatalog.UNSUPPORTED_FAULTS:
            print(f"fault: {name} is not supported by this script")
            return self.ReturnCode.NOT_SUPPORTED
        if name in NVME_FAULTS and not self._runner.has_nvme():
            print(f"fault: {name} requires NVMe but no NVMe device is specified")
            return self.ReturnCode.NVME_REQUIRED
        if name in FaultCatalog.DISABLED_FAULTS:
            print(f"fault: {name} is disabled")
            return self.ReturnCode.DISABLED
        template_dir = self._templates_dir / name
        if not self._update and not FaultTemplate(template_dir).exists():
            print(f"fault: {name}, no template in {template_dir} (run with --update to create it)")
            return self.ReturnCode.NO_TEMPLATE
        return None

    def run_fault(self, fault):
        """Run one fault test."""

        fault_id, name = fault
        # skip faults that cannot run here
        skip_code = self._skip_code(name)
        if skip_code is not None:
            return skip_code

        # generate result directory, config, and env for the fault
        result_dir = Path(tempfile.mkdtemp(prefix=f"{name}_"))
        config = result_dir / "fault_injection_dlck.yaml"
        config.write_text(self._make_config(fault_id))
        env = self._env(name)

        # run dlck with the generated config and capture output
        rc, output, errors = self._runner.run(config, env)
        (result_dir / "output.txt").write_text(output)

        # collect logs if dlck produced any
        logs_dir = result_dir / "logs"
        log_dir = re.search(rf"^Log directory: ({TemplateMatcher.LOG_DIR_RE})$",
                            output, re.MULTILINE)
        if log_dir:
            self._collect_logs(log_dir.group(1), logs_dir)

        # print summary of the fault execution
        print(f"fault: {name}, dlck exit code: {rc}, results: {result_dir}")
        if errors:
            print(errors, end="")

        template_dir = self._templates_dir / name
        template = FaultTemplate(template_dir)

        # update template if requested
        if self._update:
            template.update(output, logs_dir)
            print(f"template updated: {template_dir}")
            return self.ReturnCode.OK

        errors = template.compare(output, logs_dir)
        if errors:
            print("FAIL")
            print("\n".join(errors))
            return self.ReturnCode.FAIL
        print("PASS")
        return self.ReturnCode.OK


def main():
    """Main entry point for the fault injection test script."""
    args = PARSER.parse_args()
    catalog = FaultCatalog(TEMPLATE_CONFIG)
    helper = DlckTestHelperRunner(args.storage, args.pool_uuid) if not args.nvme else None
    runner = DlckRunner(Path(args.dlck).resolve(), args.pool_uuid, args.storage, args.nvme)
    test = FaultInjectionTest(catalog, runner, TEMPLATE_DIR, args.update)

    # select faults specified by the user if provided
    selected = [catalog.find(key) for key in args.faults] or catalog.faults
    if not selected:
        print("No fault tests selected.")
        return

    # run selected faults
    results = {}
    try:
        if helper:
            print("Create and populate a pool with standard data:")
            helper.init()
        else:
            print("Skipping pool creation and population as the MD-on-SSD mode is used.")
        print("Run fault tests:")
        for fault in selected:
            results[fault[1]] = test.run_fault(fault)
    finally:
        if helper:
            helper.fini()

    # report the results
    if len(results) == 0:
        print("\nNo test results to report.")
        return

    print("\nSummary:")
    for name, code in results.items():
        print(f"  {FaultInjectionTest.rc_to_str_with_padding(code)}{name}")

    ReturnCode = FaultInjectionTest.ReturnCode
    failed = [name for name, code in results.items() if code == ReturnCode.FAIL]
    passed = sum(1 for code in results.values() if code == ReturnCode.OK)
    skipped = len(results) - passed - len(failed)
    print(f"\nTotal: {len(results)}, pass: {passed}, skip: {skipped}, failed: {len(failed)}")
    if failed:
        print("\nFailed:\n  " + "\n  ".join(failed))
    print(f"\nResult: {'FAIL' if failed else 'OK'}")
    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
