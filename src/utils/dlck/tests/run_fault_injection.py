#!/usr/bin/env python3
"""Run dlck with one fault from fault_injection_dlck.yaml enabled and compare the results.

Usage:
  run_fault_injection.py [FAULT ...] [--update] [--dlck PATH] [--file ARG] [--storage PATH]

FAULT is a fault name (e.g. DLCK_FAULT_GETGRNAM) or its id (e.g. 131329).
Without FAULT, every fault in the template is run.
Expected results live in fault_templates/FAULT/output.txt and fault_templates/FAULT/logs/.
Template placeholders:
  {{UUID}}      any UUID (e.g. the pool UUID)
  {{LOG_DIR}}   dlck temporary log directory
  {{re:REGEX}}  any regular expression (must not contain "}}")
--update stores the results of this run as the template for FAULT.
"""

import argparse
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import yaml

TESTS_DIR = Path(__file__).resolve().parent
REPO_DIR = TESTS_DIR.parents[3]
TEMPLATE_CONFIG = TESTS_DIR / "fault_injection_dlck.yaml"
TEMPLATE_DIR = TESTS_DIR / "fault_templates"
DEFAULT_DLCK = "/opt/daos/bin/dlck"
# these faults have a very specific requirements which are not supported by this script
UNSUPPORTED_FAULTS = [
    "DLCK_MOCK_NOT_IN_DAOS_SERVER_GROUP",
    "DLCK_MOCK_NO_DAOS_SERVER_GROUP",
    "DLCK_FAULT_GETGROUPS",
    "DLCK_FAULT_GETGRNAM"]

UUID_RE = "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}"
LOG_DIR_RE = "/tmp/dlck_check_[A-Za-z0-9]{6}"
PLACEHOLDERS = {"UUID": UUID_RE, "LOG_DIR": LOG_DIR_RE}
TOKEN_RE = re.compile(r"\{\{(.+?)\}\}")
FAULT_RE = re.compile(r"^(\s*)#?\s*-\s*id:\s*(\d+)\s*#\s*(\w+)")

Parser = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
Parser.add_argument("faults", nargs="*", metavar="FAULT",
                    help="fault name or id (default: all faults in the template)")
Parser.add_argument("--update", action="store_true",
                    help="store the results of this run as the template")
Parser.add_argument("--dlck", default=str(DEFAULT_DLCK), help="path to the dlck binary")
Parser.add_argument("--file", required=True, help="dlck --file argument")
Parser.add_argument("--storage", required=True, help="dlck --storage argument")


def read_template():
    """Return the (id, name) of each fault in the template."""
    faults = []
    for line in TEMPLATE_CONFIG.read_text().splitlines():
        match = FAULT_RE.match(line)
        if match:
            faults.append((match.group(2), match.group(3)))
    return faults


def find_fault(faults, key):
    """Return the (id, name) entry matching a fault name or id."""
    for fault in faults:
        if key in fault:
            return fault
    raise SystemExit(f"fault '{key}' not found in {TEMPLATE_CONFIG}")


def make_config(fault_id):
    """Return the YAML config text that enables only the given fault."""
    return yaml.safe_dump({"fault_config": [{"id": int(fault_id)}]}, sort_keys=False)


def run_dlck(dlck, file_arg, storage, config):
    """Run dlck with the fault config, optionally as root; return the exit code and stdout."""
    env = dict(os.environ, D_FI_CONFIG=str(config))
    cmd = ["sudo", "-E"]
    cmd += [str(dlck), "--file", file_arg, "--storage", storage]
    proc = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, text=True)
    return proc.returncode, proc.stdout


def collect_logs(log_dir, dest):
    """Copy dlck's log directory to dest and remove the original."""
    subprocess.run(["sudo", "cp", "-r", log_dir, str(dest)], check=True)
    subprocess.run(["sudo", "chown", "-R", f"{os.getuid()}:{os.getgid()}", str(dest)], check=True)
    subprocess.run(["sudo", "rm", "-rf", log_dir], check=True)


def to_regex(text):
    """Convert a template line into a regex that must fully match the actual line."""
    parts = []
    pos = 0
    for match in TOKEN_RE.finditer(text):
        parts.append(re.escape(text[pos:match.start()]))
        key = match.group(1)
        if key.startswith("re:"):
            parts.append(key[3:])
        elif key in PLACEHOLDERS:
            parts.append(PLACEHOLDERS[key])
        else:
            raise ValueError(f"unknown template placeholder: {key}")
        pos = match.end()
    parts.append(re.escape(text[pos:]))
    return re.compile("".join(parts))


def normalize(text):
    """Replace the variable parts of captured text with template placeholders."""
    text = re.sub(UUID_RE, "{{UUID}}", text)
    return re.sub(LOG_DIR_RE, "{{LOG_DIR}}", text)


def compare_text(name, expected, actual):
    """Return mismatch messages between a template and the actual text, line by line."""
    errors = []
    exp_lines = expected.splitlines()
    act_lines = actual.splitlines()
    if len(exp_lines) != len(act_lines):
        errors.append(f"{name}: {len(act_lines)} line(s), template has {len(exp_lines)}")
    for num, (exp, act) in enumerate(zip(exp_lines, act_lines), 1):
        if not to_regex(exp).fullmatch(act):
            errors.append(f"{name}:{num}: expected '{exp}', got '{act}'")
    return errors


def compare_logs(expected_dir, actual_dir):
    """Match log files by (pattern) name and compare their contents."""
    expected = {p.name: p for p in expected_dir.iterdir()} if expected_dir.is_dir() else {}
    remaining = [p.name for p in actual_dir.iterdir()] if actual_dir.is_dir() else []
    errors = []
    for name in sorted(expected):
        rx = to_regex(name)
        match = next((a for a in sorted(remaining) if rx.fullmatch(a)), None)
        if match is None:
            errors.append(f"missing log file matching '{name}'")
            continue
        remaining.remove(match)
        errors += compare_text(f"logs/{match}", expected[name].read_text(),
                               (actual_dir / match).read_text())
    errors += [f"unexpected log file '{name}'" for name in sorted(remaining)]
    return errors


def update_template(template, output, logs_dir):
    """Store the normalized output and log files as the template."""
    shutil.rmtree(template / "logs", ignore_errors=True)
    template.mkdir(parents=True, exist_ok=True)
    (template / "output.txt").write_text(normalize(output))
    if logs_dir.is_dir():
        (template / "logs").mkdir()
        for log in logs_dir.iterdir():
            (template / "logs" / normalize(log.name)).write_text(normalize(log.read_text()))


def run_fault(fault, args):
    """Run one fault and return 0 (PASS or updated), 1 (FAIL) or 2 (no template)."""

    # generate result directory and config for the fault
    fault_id, name = fault
    if name in UNSUPPORTED_FAULTS:
        print(f"fault {name} is not supported by this script")
        return 2
    result_dir = Path(tempfile.mkdtemp(prefix=f"{name}_"))
    config = result_dir / "fault_injection_dlck.yaml"
    config.write_text(make_config(fault_id))

    # run dlck with the generated config and capture output
    rc, output = run_dlck(Path(args.dlck).resolve(), args.file, args.storage, config)
    (result_dir / "output.txt").write_text(output)

    # collect logs if dlck produced any
    logs_dir = result_dir / "logs"
    log_dir = re.search(rf"^Log directory: ({LOG_DIR_RE})$", output, re.MULTILINE)
    if log_dir:
        collect_logs(log_dir.group(1), logs_dir)

    # print summary of the fault execution
    print(f"fault: {name}, dlck exit code: {rc}, results: {result_dir}")

    template = TEMPLATE_DIR / name

    # update template if requested
    if args.update:
        update_template(template, output, logs_dir)
        print(f"template updated: {template}")
        return 0

    # check if template exists
    if not (template / "output.txt").is_file():
        print(f"no template for {name} in {template} (run with --update to create it)")
        return 2

    errors = compare_text("output.txt", (template / "output.txt").read_text(), output)
    errors += compare_logs(template / "logs", logs_dir)
    if errors:
        print("FAIL")
        print("\n".join(errors))
        return 1
    print("PASS")
    return 0


def main():
    args = Parser.parse_args()

    faults = read_template()
    selected = [find_fault(faults, key) for key in args.faults] or faults
    if not selected:
        raise SystemExit(f"no faults found in {TEMPLATE_CONFIG}")

    results = {}
    for fault in selected:
        results[fault[1]] = run_fault(fault, args)
    if len(results) > 1:
        labels = {0: "OK", 1: "FAIL", 2: "NO TEMPLATE"}
        print("\nSummary:")
        for name, code in results.items():
            print(f"  {name}: {labels[code]}")
    return max(results.values())


if __name__ == "__main__":
    raise SystemExit(main())
