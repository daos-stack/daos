#!/usr/bin/env python3
"""
  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
  All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

  Summarize functional test failures from JUnit/cmocka XML files.

  Scans a test-results/ directory and builds a GitHub-flavoured Markdown
  section for GITHUB_STEP_SUMMARY that lists every failing test case with
  its suite and name.

  Two granularity levels are handled automatically:
    - cmocka-level  (UTEST_*.xml): individual test case names
                    e.g. test_d_rank_list_to_str
    - binary-level  (test_*.xml):  test binary basename
                    e.g. test_gurt

  cmocka-level entries take precedence when available (they are more
  specific): a suite's binary-level entry is dropped as redundant when that
  suite already has a cmocka-level entry AND exactly one binary failed in
  that suite (with more than one failing binary in the same suite, it's not
  possible to tell which one is already covered, so none are dropped).  The
  aggregate summary file test_run_utest.py.native.xml is skipped.

  Usage (from .github/workflows/unit-test-template.yml, called by unit-testing.yml):

    python3 utils/ci/summarize_functional_failures.py \\
        --results-dir test-results

  Exits 1 when at least one failure is found (so the caller can gate on it),
  0 when all tests passed.
"""

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def _suite_from_classname(classname: str) -> str:
    """Extract suite name from a JUnit classname attribute.

    Examples:
      "UTEST_gurt.gurt_tests"  → "gurt"
      "gurt"                   → "gurt"
      "UTEST_cart.cart"        → "cart"
    """
    m = re.match(r'UTEST_([^.]+)', classname)
    return m.group(1) if m else classname.split('.')[0]


def collect_failures(results_dir: Path) -> list:
    """Scan *results_dir* for JUnit XML files and return a list of failure dicts.

    Each dict has:
      suite      str   — test suite name (e.g. "gurt")
      name       str   — test case name: cmocka function OR binary path
      is_binary  bool  — True when name is a binary path (contains '/')
    """
    failures = []
    for xml_path in sorted(results_dir.glob('*.xml')):
        fname = xml_path.name
        if 'run_utest.py' in fname:  # skip aggregate summary
            continue
        try:
            root = ET.parse(xml_path).getroot()
        except ET.ParseError:
            continue
        for tc in root.iter('testcase'):
            fail_el = tc.find('failure')
            err_el = tc.find('error')
            if fail_el is None and err_el is None:
                continue
            tc_name = tc.get('name', '')
            classname = tc.get('classname', '')
            failures.append({
                'suite': _suite_from_classname(classname),
                'name': tc_name,
                'is_binary': '/' in tc_name,
            })
    return failures


def build_summary_md(failures: list) -> str:
    """Return a Markdown summary section for the given failure list."""
    if not failures:
        return '#### ✅ All functional unit tests passed\n'

    # cmocka-level entries (specific test function names) take precedence over
    # binary-level entries for the same suite.
    cmocka_entries = [f for f in failures if not f['is_binary']]
    binary_entries = [f for f in failures if f['is_binary']]

    # A suite's binary-level entry is only known to be fully redundant with its
    # cmocka-level entry/entries when exactly one binary failed in that suite --
    # with more than one, we can't tell which is already covered by a cmocka-level
    # entry, so nothing is suppressed for that suite in that (rarer) case.
    cmocka_suites = {f['suite'] for f in cmocka_entries}
    binary_names_by_suite: dict = {}
    for f in binary_entries:
        binary_names_by_suite.setdefault(f['suite'], set()).add(f['name'])
    binary_entries = [
        f for f in binary_entries
        if not (f['suite'] in cmocka_suites and len(binary_names_by_suite[f['suite']]) == 1)
    ]

    # Deduplicate while preserving order (e.g. accidental double-reporting of the
    # exact same entry).
    seen: set = set()
    items: list = []
    for f in cmocka_entries + binary_entries:
        key = (f['suite'], f['name'])
        if key not in seen:
            seen.add(key)
            items.append(f)

    lines = [f'#### ❌ Functional test failures — {len(items)} failure(s)', '']
    lines += [
        '| Suite | Test |',
        '|-------|------|',
    ]
    for item in items:
        if item['is_binary']:
            display = f'`{os.path.basename(item["name"])}`'
        else:
            display = f'`{item["name"]}`'
        lines.append(f'| {item["suite"]} | {display} |')

    return '\n'.join(lines) + '\n'


def main() -> int:
    """Entry point."""
    parser = argparse.ArgumentParser(
        description='Summarize functional test failures for GITHUB_STEP_SUMMARY')
    parser.add_argument('--results-dir', required=True,
                        help='Directory containing test result XML files')
    parser.add_argument('--summary-out', default=None,
                        help='Output path for the Markdown file (default: stdout)')
    args = parser.parse_args()

    results_dir = Path(args.results_dir)
    if not results_dir.is_dir():
        print(f'Results directory not found: {results_dir}', file=sys.stderr)
        return 0

    failures = collect_failures(results_dir)
    summary = build_summary_md(failures)

    if args.summary_out:
        Path(args.summary_out).write_text(summary, encoding='utf-8')
    else:
        print(summary, end='')

    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
