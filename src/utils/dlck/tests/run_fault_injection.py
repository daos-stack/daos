#!/usr/bin/env python3
"""
Copyright 2026 Hewlett Packard Enterprise Development LP
All rights reserved.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import yaml

# defaults and paths
DEFAULT_DLCK = "/opt/daos/bin/dlck"
TESTS_DIR = Path(__file__).resolve().parent
TEMPLATE_CONFIG = TESTS_DIR / "fault_injection_dlck.yaml"
TEMPLATE_DIR = TESTS_DIR / "fault_templates"


# usage and arguments
USAGE = """Run dlck with one fault from fault_injection_dlck.yaml enabled and compare the results against the template.

FAULT is a fault name (e.g. DLCK_FAULT_GETGRNAM) or its id (e.g. 131329).
Without FAULT, every fault in the template is run.
Expected results live in fault_templates/FAULT/output.txt and fault_templates/FAULT/logs/.
Template placeholders:
  {{UUID}}      any UUID (e.g. the pool UUID)
  {{LOG_DIR}}   dlck temporary log directory
"""  # noqa: E501 line too long
Parser = argparse.ArgumentParser(description=USAGE,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
Parser.add_argument("faults", nargs="*", metavar="FAULT",
                    help="fault name or id (default: all faults in the template)")
Parser.add_argument("--update", action="store_true",
                    help="store the results of this run as the template")
Parser.add_argument("--dlck", default=str(DEFAULT_DLCK), help="path to the dlck binary")
Parser.add_argument("--file", required=True, help="dlck --file argument")
Parser.add_argument("--storage", required=True, help="dlck --storage argument")
Parser.add_argument("--nvme", default=None, help="dlck --nvme argument")


class FaultCatalog:
    """Reads the faults listed in the template config."""

    # these faults have a very specific requirements which are not supported by this script
    UNSUPPORTED_FAULTS = [
        "DLCK_MOCK_NOT_IN_DAOS_SERVER_GROUP",
        "DLCK_MOCK_NO_DAOS_SERVER_GROUP",
        "DLCK_FAULT_GETGROUPS",
        "DLCK_FAULT_GETGRNAM"]

    NVME_FAULTS = ["DLCK_FAULT_POOL_OPEN_BIO"]

    # '  # - id: 131333 # DLCK_FAULT_CREATE_LOG_DIR'
    _FAULT_RE = re.compile(r"^\s*#?\s*-\s*id:\s*(\d+)\s*#\s*(\w+)")

    def __init__(self, config_path):
        self._config_path = config_path
        self._faults = self._read_faults()

    @property
    def faults(self):
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


class DlckRunner:
    """Runs dlck under sudo."""

    def __init__(self, dlck, file_arg, storage, nvme=None):
        self._dlck = dlck
        self._file_arg = file_arg
        self._storage = storage
        self._nvme = nvme

    def has_nvme(self):
        return self._nvme is not None

    def run(self, config):
        """Run dlck with the fault config; return the exit code and stdout."""
        env = dict(os.environ, D_FI_CONFIG=str(config))
        cmd = ["sudo", "-E", str(self._dlck), "--file", self._file_arg,
               "--storage", self._storage, "--targets", "1"]
        if self._nvme is not None:
            cmd += ["--nvme", self._nvme]
        proc = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, text=True)
        return proc.returncode, proc.stdout


class TemplateMatcher:
    """Matches actual dlck output against template text with placeholders."""

    LOG_DIR_RE = "/tmp/dlck_check_[A-Za-z0-9]{6}"
    _UUID_RE = "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}"

    @classmethod
    def normalize(cls, text):
        """Replace the variable parts of captured text with template placeholders."""
        text = re.sub(cls._UUID_RE, "{{UUID}}", text)
        return re.sub(cls.LOG_DIR_RE, "{{LOG_DIR}}", text)

    @classmethod
    def compare_text(cls, name, expected, actual):
        """Return mismatch messages between a template and the actual text, line by line."""
        errors = []
        exp_lines = expected.splitlines()
        act_lines = cls.normalize(actual).splitlines()
        if len(exp_lines) != len(act_lines):
            errors.append(f"{name}: {len(act_lines)} line(s), template has {len(exp_lines)}")
        # enumerate(..., 1) starts line numbering at 1
        for num, (exp, act) in enumerate(zip(exp_lines, act_lines), 1):
            if exp != act:
                errors.append(f"{name}:{num}: expected '{exp}', got '{act}'")
        return errors

    @classmethod
    def compare_logs(cls, expected_dir, actual_dir):
        """Match log files by name and compare their contents."""
        expected = {p.name: p for p in expected_dir.iterdir()} if expected_dir.is_dir() else {}
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
        self._logs_dir = path / "logs"

    def exists(self):
        return self._output_file.is_file()

    def update(self, output, logs_dir):
        """Store the normalized output and log files as the template."""
        shutil.rmtree(self._logs_dir, ignore_errors=True)
        self._path.mkdir(parents=True, exist_ok=True)
        self._output_file.write_text(TemplateMatcher.normalize(output))
        if logs_dir.is_dir():
            self._logs_dir.mkdir()
            for log in logs_dir.iterdir():
                name = TemplateMatcher.normalize(log.name)
                (self._logs_dir / name).write_text(TemplateMatcher.normalize(log.read_text()))

    def compare(self, output, logs_dir):
        """Return mismatch messages between this template and the actual output and logs."""
        errors = TemplateMatcher.compare_text("output.txt", self._output_file.read_text(), output)
        errors += TemplateMatcher.compare_logs(self._logs_dir, logs_dir)
        return errors


class FaultInjectionTest:
    """Runs faults through dlck and compares or stores the results."""

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
    def _collect_logs(log_dir, dest):
        """Copy dlck's log directory to dest and remove the original."""
        subprocess.run(["sudo", "cp", "-r", log_dir, str(dest)], check=True)
        subprocess.run(["sudo", "chown", "-R", f"{os.getuid()}:{os.getgid()}",
                        str(dest)], check=True)
        subprocess.run(["sudo", "rm", "-rf", log_dir], check=True)

    def run_fault(self, fault):
        """Run one fault; return 0 (PASS/updated), 1 (FAIL) or 2 (no template or unsupported)."""

        # generate result directory and config for the fault
        fault_id, name = fault
        if name in FaultCatalog.UNSUPPORTED_FAULTS:
            print(f"fault {name} is not supported by this script")
            return 2
        if name in FaultCatalog.NVME_FAULTS and not self._runner.has_nvme():
            print(f"fault {name} requires NVMe but no NVMe device is specified")
            return 2
        result_dir = Path(tempfile.mkdtemp(prefix=f"{name}_"))
        config = result_dir / "fault_injection_dlck.yaml"
        config.write_text(self._make_config(fault_id))

        # run dlck with the generated config and capture output
        rc, output = self._runner.run(config)
        (result_dir / "output.txt").write_text(output)

        # collect logs if dlck produced any
        logs_dir = result_dir / "logs"
        log_dir = re.search(rf"^Log directory: ({TemplateMatcher.LOG_DIR_RE})$",
                            output, re.MULTILINE)
        if log_dir:
            self._collect_logs(log_dir.group(1), logs_dir)

        # print summary of the fault execution
        print(f"fault: {name}, dlck exit code: {rc}, results: {result_dir}")

        template_dir = self._templates_dir / name
        template = FaultTemplate(template_dir)

        # update template if requested
        if self._update:
            template.update(output, logs_dir)
            print(f"template updated: {template_dir}")
            return 0

        # check if template exists
        if not template.exists():
            print(f"no template for {name} in {template_dir} (run with --update to create it)")
            return 2

        errors = template.compare(output, logs_dir)
        if errors:
            print("FAIL")
            print("\n".join(errors))
            return 1
        print("PASS")
        return 0


def main():
    args = Parser.parse_args()
    catalog = FaultCatalog(TEMPLATE_CONFIG)
    runner = DlckRunner(Path(args.dlck).resolve(), args.file, args.storage, args.nvme)
    test = FaultInjectionTest(catalog, runner, TEMPLATE_DIR, args.update)

    # select faults specified by the user if provided
    selected = [catalog.find(key) for key in args.faults] or catalog.faults

    # run selected faults
    results = {}
    for fault in selected:
        results[fault[1]] = test.run_fault(fault)

    # report the results
    if len(results) > 1:
        labels = {0: "OK", 1: "FAIL", 2: "NO TEMPLATE"}
        print("\nSummary:")
        for name, code in results.items():
            print(f"  {name}: {labels[code]}")


if __name__ == "__main__":
    main()
