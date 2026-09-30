#!/usr/bin/env python3
"""
(C) Copyright 2026 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent

Standalone script to run layout_test with a given YAML configuration
and capture output to a text file.
"""

import argparse
import json
import os
import re
import subprocess  # nosec
import sys
from datetime import datetime
from pathlib import Path

import yaml

ANSI_GREEN = "\033[32m"
ANSI_RED = "\033[31m"
ANSI_RESET = "\033[0m"
SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_YAML_PATH = SCRIPT_DIR / "layout_test_run.yaml"


def colorize(text, color):
    """Return colorized text when stdout supports ANSI colors."""
    if not sys.stdout.isatty() or os.environ.get("NO_COLOR") is not None:
        return text
    return f"{color}{text}{ANSI_RESET}"


def find_binary(binary_name="layout_test"):
    """Locate layout_test in PREFIX/bin."""
    this_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(this_dir, "..", "..", ".."))
    install_bin = os.path.join(repo_root, "install", "bin", binary_name)
    json_file = ".build_vars.json"
    path = os.path.join(repo_root, json_file)
    if os.path.exists(path):
        with open(path, "r") as ofh:
            conf = json.load(ofh)
        install_bin = os.path.join(conf["PREFIX"], "bin", binary_name)
    if os.path.isfile(install_bin) and os.access(install_bin, os.X_OK):
        return install_bin

    raise FileNotFoundError(
        f"Expected executable not found: {install_bin}. "
        "Build and install DAOS so PREFIX/bin/layout_test exists."
    )


def load_yaml(yaml_file):
    """Load and parse YAML configuration file."""
    with open(yaml_file, "r") as f:
        data = yaml.safe_load(f)
    if data is None:
        raise ValueError(f"YAML file is empty: {yaml_file}")
    if not isinstance(data, dict):
        raise ValueError(f"YAML root must be a mapping/object: {yaml_file}")
    return data


def parse_config_file(yaml_file):
    """Parse YAML configuration and return setup data."""
    yaml_path = Path(yaml_file).resolve()
    if not yaml_path.exists():
        print(f"Error: YAML file not found: {yaml_file}", file=sys.stderr)
        sys.exit(1)

    data = load_yaml(str(yaml_path))
    return yaml_path, data


def prepare_output_file(yaml_path, output_file):
    """Generate or validate output file path."""
    if output_file is None:
        base_name = yaml_path.stem
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        return yaml_path.parent / f"{base_name}_{timestamp}.txt"
    return Path(output_file)


def build_test_cases(topologies, variants, obj_count):
    """Generate all test cases from topology and variant configurations."""
    test_cases = []
    for topo_name, topo in topologies.items():
        if not isinstance(topo, dict):
            raise ValueError(f"Topology '{topo_name}' must be a mapping")
        nodes = int(topo["nodes"])
        ranks = int(topo["ranks"])
        targets = int(topo["targets"])

        for var_name, variant in variants.items():
            if not isinstance(variant, dict):
                raise ValueError(f"Object class variant '{var_name}' must be a mapping")
            obj_class = variant["object_class"]
            operation = variant["operation"]

            test_cases.append(
                {
                    "topo_name": topo_name,
                    "var_name": var_name,
                    "nodes": nodes,
                    "ranks": ranks,
                    "targets": targets,
                    "obj_class": obj_class,
                    "operation": operation,
                    "obj_count": obj_count,
                }
            )
    return test_cases


def extract_config_values(data):
    """Extract and validate configuration values from parsed YAML."""
    obj_count = data.get("obj_count", 100000)

    # Parse topology variants
    topologies = data.get(
        "topology", {"n128_r1_t16": {"nodes": 128, "ranks": 1, "targets": 16}}
    )
    if not isinstance(topologies, dict):
        topologies = {"default": topologies}
    if not topologies:
        raise ValueError("No topology configuration found in YAML")

    # Parse object class variants
    variants = data.get("object_class", {"ec_2p1_s2_g1": {"object_class": "EC_2P1G1"}})
    if not isinstance(variants, dict):
        variants = {"default": variants}
    if not variants:
        raise ValueError("No object_class configuration found in YAML")

    # Read timeout from YAML; 0 means unlimited
    yaml_timeout = data.get("timeouts", {}).get("test_layout_performance", 300)
    timeout = None if yaml_timeout == 0 else yaml_timeout

    return obj_count, topologies, variants, timeout


def extract_total_ms(output_text):
    """Extract total runtime in milliseconds from layout_test output."""
    match = re.search(r"total\s*=\s*([0-9]+(?:\.[0-9]+)?)\s*ms", output_text)
    return match.group(1) if match else None


def extract_failure_reason(output_text):
    """Extract the most useful failure reason from layout_test output."""
    lines = output_text.splitlines()

    # Prefer a full ERROR/FATAL block so we preserve the primary error and details.
    for index, line in enumerate(lines):
        if re.search(r"\b(ERROR|FATAL):", line, re.IGNORECASE):
            block = [line.rstrip()]
            for follow in lines[index + 1:]:
                if not follow.strip():
                    break
                if re.match(r"^\s+", follow):
                    block.append(follow.rstrip())
                    continue
                break
            return "\n".join(block)

    for pattern in (r"^\s*.*unknown object class.*$",):
        match = re.search(pattern, output_text, re.MULTILINE | re.IGNORECASE)
        if match:
            return match.group(0).rstrip()

    non_empty_lines = [line.rstrip() for line in lines if line.strip()]
    if non_empty_lines:
        return non_empty_lines[-1]
    return "Unknown failure"


def normalize_reason_lines(reason_text):
    """Return non-empty reason lines with consistent whitespace."""
    return [line.strip() for line in reason_text.splitlines() if line.strip()]


def run_single_test(outf, test, test_num, test_count, iteration, iterations,
                    run_num, total_runs, binary, timeout):
    """Run a single test and return whether it passed."""
    outf.write(f"\n{'='*80}\n")
    outf.write(
        f"Test Case {test_num}/{test_count}, "
        f"Iteration {iteration}/{iterations} (Run {run_num}/{total_runs})\n"
    )
    outf.write(f"Topology: {test['topo_name']}")
    outf.write(
        f" (n={test['nodes']}, r={test['ranks']}, t={test['targets']})\n"
    )
    outf.write(f"Variant: {test['var_name']} ({test['obj_class']})\n")
    outf.write(f"Operation: {test['operation']}\n")
    outf.write(f"Object count: {test['obj_count']}\n")
    outf.write(f"{'='*80}\n\n")

    cmd = [
        binary,
        "-o",
        test["operation"],
        "-n",
        str(test["nodes"]),
        "-r",
        str(test["ranks"]),
        "-t",
        str(test["targets"]),
        "-c",
        test["obj_class"],
        "-N",
        str(test["obj_count"]),
    ]

    print(
        f"\n[{run_num}/{total_runs}] Topology: n={test['nodes']}, "
        f"r={test['ranks']}, t={test['targets']}"
    )
    print(f"Variant: {test['obj_class']}")
    print(f"Running: {' '.join(cmd)}")
    outf.write(f"Command: {' '.join(cmd)}\n\n")
    outf.flush()

    failure_info = None
    try:
        result = subprocess.run(  # nosec
            cmd, capture_output=True, text=True, timeout=timeout, check=False
        )
        if result.stdout:
            outf.write(result.stdout)
        if result.stderr:
            outf.write(result.stderr)
        outf.write(f"\nExit code: {result.returncode}\n")

        combined_output = (result.stdout or "") + "\n" + (result.stderr or "")
        total_ms = extract_total_ms(combined_output)
        if result.returncode != 0:
            failure_reason = extract_failure_reason(combined_output)
            reason_lines = normalize_reason_lines(failure_reason)
            full_reason = (
                ". ".join(reason_lines) if reason_lines else "Unknown failure"
            )
            failure_msg = (
                f"FAILED run {run_num}/{total_runs}: "
                f"topology={test['topo_name']}, variant={test['var_name']}, "
                f"iteration={iteration}, exit_code={result.returncode}, "
                f"{full_reason}"
            )
            failure_info = (failure_msg, reason_lines)
            outf.write(f"{failure_msg}\n")
            print(f"  iter {iteration:02d}: {colorize('FAIL', ANSI_RED)}")
            for reason_line in reason_lines:
                print(f"    {reason_line}")
        elif total_ms is not None:
            print(
                f"  iter {iteration:02d}: "
                f"{colorize('PASS', ANSI_GREEN)} total={total_ms} ms"
            )
        else:
            print(f"  iter {iteration:02d}: {colorize('PASS', ANSI_GREEN)}")
    except subprocess.TimeoutExpired:
        outf.write(f"\nTest TIMEOUT ({timeout}s exceeded)\n")
        print("  WARNING: Test timed out")
        failure_msg = (
            f"TIMEOUT run {run_num}/{total_runs}: "
            f"topology={test['topo_name']}, variant={test['var_name']}, "
            f"iteration={iteration}, timeout={timeout}s"
        )
        failure_info = (failure_msg, [])
        print(
            f"  iter {iteration:02d}: "
            f"{colorize('FAIL', ANSI_RED)} timeout={timeout}s"
        )
    except (OSError, subprocess.SubprocessError) as e:
        outf.write(f"\nError running test: {e}\n")
        print(f"  ERROR: {e}", file=sys.stderr)
        failure_msg = (
            f"ERROR run {run_num}/{total_runs}: "
            f"topology={test['topo_name']}, variant={test['var_name']}, "
            f"iteration={iteration}, error={e}"
        )
        failure_info = (failure_msg, [])
        print(f"  iter {iteration:02d}: {colorize('FAIL', ANSI_RED)} error={e}")

    return failure_info


def run_tests(yaml_file, output_file=None, iterations=1):
    """Run layout_test for each variant in the YAML config.

    Args:
        yaml_file: Path to YAML configuration file
        output_file: Optional output file path. If None, generated from yaml_file.
        iterations: Number of times to run each test case (default: 1)
    """
    yaml_path, data = parse_config_file(yaml_file)
    obj_count, topologies, variants, timeout = extract_config_values(data)
    output_file = prepare_output_file(yaml_path, output_file)

    print(f"Loading config from: {yaml_path}")
    print(f"Output will be written to: {output_file}")

    # Find the binary
    try:
        binary = find_binary("layout_test")
        print(f"Found layout_test binary at: {binary}")
        print(f"Number of OIDs: {obj_count}")
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    # Build test cases
    test_cases = build_test_cases(topologies, variants, obj_count)
    if not test_cases:
        raise ValueError("No test cases were generated from YAML configuration")

    print(f"Found {len(test_cases)} test case(s), {iterations} iteration(s) each")
    failures = []

    with open(output_file, "w") as outf:
        outf.write(f"Layout Test Run - {datetime.now().isoformat()}\n")
        outf.write(f"Configuration: {yaml_path}\n")
        outf.write(f"Iterations: {iterations}\n")
        outf.write("=" * 80 + "\n\n")

        total_runs = len(test_cases) * iterations
        run_num = 0

        for test_num, test in enumerate(test_cases, 1):
            for iteration in range(1, iterations + 1):
                run_num += 1
                failure_info = run_single_test(
                    outf, test, test_num, len(test_cases), iteration, iterations,
                    run_num, total_runs, binary, timeout
                )
                if failure_info:
                    failures.append(failure_info[0])

    if failures:
        print(f"\nAll tests completed with failures. Output saved to: {output_file}")
        for failure in failures:
            print(failure)
        return 1
    print(f"\nAll tests completed. Output saved to: {output_file}")
    return 0


if __name__ == "__main__":
    default_yaml_path = DEFAULT_YAML_PATH

    parser = argparse.ArgumentParser(
        description="Run layout_test with a given YAML configuration and capture output"
    )
    parser.add_argument(
        "-y",
        "--yaml",
        dest="yaml_file",
        default=str(default_yaml_path),
        help=(
            "Path to layout test YAML configuration " f"(default: {default_yaml_path})"
        ),
    )
    parser.add_argument(
        "-o",
        "--output",
        dest="output_file",
        default=None,
        help="Output file path (default: <yaml_stem>_<timestamp>.txt)",
    )
    parser.add_argument(
        "-i",
        "--iterations",
        type=int,
        default=1,
        help="Number of iterations per test case (default: 1)",
    )

    args = parser.parse_args()

    if args.iterations < 1:
        print("Error: iterations must be >= 1", file=sys.stderr)
        sys.exit(1)

    sys.exit(run_tests(args.yaml_file, args.output_file, args.iterations))
