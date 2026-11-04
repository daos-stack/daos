#!/usr/bin/env python3
"""Functional tests for SCons clean behavior."""

import os
import shlex
import shutil
import subprocess  # nosec
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
BUILD_TYPES = ("debug", "dev", "release")
TARGET_TYPES = ("default", "debug", "dev", "release")
BUILD_DEPS_MODES = ("no", "yes", "only", "fetch")
STATE_FILES = ("daos.conf", ".sconsign.dblite", "config.log")


class SConsCleanTest(unittest.TestCase):
    """Test clean behavior against the project's real SConstruct files."""

    @classmethod
    def setUpClass(cls):
        scons_command = shlex.split(os.environ.get("SCONS", "scons"))
        if not scons_command or shutil.which(scons_command[0]) is None:
            raise unittest.SkipTest("scons is not available; activate the DAOS build environment")

        cls.scons_command = scons_command
        cls.temp_dir = tempfile.TemporaryDirectory(prefix="daos-scons-clean-")
        cls.test_root = Path(cls.temp_dir.name)
        cls.build_root = cls.test_root / "build"
        cls.prefix = cls.test_root / "install"
        cls.build_root.mkdir()
        cls.prefix.mkdir()

        cls.state_dir = cls.test_root / "state"
        cls.state_dir.mkdir()
        cls.original_state = {}
        for name in STATE_FILES:
            source = REPO_ROOT / name
            cls.original_state[name] = source.exists()
            if source.exists():
                shutil.copy2(source, cls.state_dir / name)

        cls.addClassCleanup(cls._restore_state)

    @classmethod
    def _restore_state(cls):
        for name, existed in cls.original_state.items():
            destination = REPO_ROOT / name
            if existed:
                shutil.copy2(cls.state_dir / name, destination)
            else:
                destination.unlink(missing_ok=True)
        cls.temp_dir.cleanup()

    def _run_clean(self, mode, build_type, target_type):
        command = [
            *self.scons_command,
            "-n",
            "-c",
            f"--build-deps={mode}",
            f"BUILD_TYPE={build_type}",
            f"TARGET_TYPE={target_type}",
            f"BUILD_ROOT={self.build_root}",
            f"PREFIX={self.prefix}",
        ]
        return subprocess.run(  # nosec B603
            command,
            cwd=REPO_ROOT,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )

    def test_clean_option_matrix(self):
        """Verify all build dependency, build type, and target type combinations."""
        for mode in BUILD_DEPS_MODES:
            for build_type in BUILD_TYPES:
                for target_type in TARGET_TYPES:
                    with self.subTest(mode=mode, build_type=build_type,
                                      target_type=target_type):
                        result = self._run_clean(mode, build_type, target_type)
                        self.assertEqual(result.returncode, 0, result.stdout)

                        effective_type = build_type if target_type == "default" else target_type
                        build_delete = f'Delete("{self.build_root}/external/{effective_type}")'
                        prefix_delete = f'Delete("{self.prefix}/prereq/{effective_type}")'

                        if mode == "no":
                            self.assertNotIn(build_delete, result.stdout)
                            self.assertNotIn(prefix_delete, result.stdout)
                        else:
                            self.assertIn(build_delete, result.stdout)
                            self.assertIn(prefix_delete, result.stdout)

                        if mode == "only":
                            self.assertIn("Exiting because --build-deps=only was set",
                                          result.stdout)
                        else:
                            self.assertIn("scons: done cleaning targets.", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
