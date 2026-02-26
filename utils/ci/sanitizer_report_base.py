#!/usr/bin/env python3
"""
  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
  All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

  Shared base module for sanitizer report parsers (ASan, UBSan, TSan).

  Provides the common infrastructure used by parse_asan_reports.py,
  parse_ubsan_reports.py, and parse_tsan_reports.py:

    - StackFrame data class
    - FILE_LINE_COL / ADDR_FRAME_RE -- shared frame-regex fragments
    - SARIF 2.1.0 constants
    - collect_reports()       -- scan a log directory for <prefix>.<pid> files
    - resolve_frame_path()    -- resolve an absolute path to source-root-relative
    - resolve_paths_frames()  -- resolve rel_file on a flat frames list (ASan/UBSan)
    - resolve_paths_threaded() -- resolve rel_file in TSan's thread→frames structure
    - sarif_location()        -- build a SARIF physicalLocation dict
    - build_sarif_doc()       -- assemble the outer SARIF 2.1.0 skeleton
    - build_summary_md()      -- shared Markdown summary template for all parsers
    - report_heading()        -- shared per-report details block (used directly
                                 as build_summary_md's details_fn by all three)
    - get_args()              -- parse the four standard CLI arguments
    - main_runner()           -- shared control flow used by each parser main()
"""

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional

# ── Shared data structure ─────────────────────────────────────────────────────


@dataclass
class StackFrame:
    """One frame in a sanitizer stack trace (ASan, UBSan, or TSan)."""

    index: int
    function: str
    file: Optional[str]      # absolute path as reported by the sanitizer
    line: Optional[int]
    column: Optional[int]
    rel_file: Optional[str]  # relative to source_root, filled in later


# ── Shared frame-regex fragments ──────────────────────────────────────────────

# Recognized source file extensions when matching a frame's file:line[:col].
_SRC_EXT = r"c|cc|cpp|cxx|h|hpp"

# A "<file>:<line>[:<col>]" fragment, common to every sanitizer's frame format.
FILE_LINE_COL = rf"(?P<file>[^:()\s]+\.(?:{_SRC_EXT})):(?P<line>\d+)(?::(?P<col>\d+))?"

# ASan and UBSan share the exact same "#N 0xADDR in func file:line[:col]" frame
# format (UBSan only produces this when UBSAN_OPTIONS includes
# print_stacktrace=1). TSan's format lacks the "0xADDR" address and has a
# trailing "(binary+0xaddr)", so it composes its own regex from FILE_LINE_COL
# instead of reusing this one.
ADDR_FRAME_RE = re.compile(
    rf"^\s+#(?P<idx>\d+)\s+0x[0-9a-f]+"
    rf"\s+in\s+(?P<func>\S+)"
    rf"(?:\s+{FILE_LINE_COL})?",
    re.IGNORECASE,
)


# ── SARIF 2.1.0 constants ─────────────────────────────────────────────────────

_SARIF_SCHEMA = (
    "https://raw.githubusercontent.com/oasis-tcs/sarif-spec"
    "/master/Schemata/sarif-schema-2.1.0.json"
)
_SARIF_VERSION = "2.1.0"
_TOOL_VERSION = "1.0"


# ── Log file collection ───────────────────────────────────────────────────────

def collect_reports(report_dir: Path, prefix: str,
                    parse_fn: Callable) -> list:
    """Scan *report_dir* for files named ``<prefix>.<pid>`` and parse each one.

    *parse_fn* must accept a ``Path`` and return either a single report object
    or a list of report objects (both styles are supported).

    After parsing each file this function also looks for a companion
    ``<filename>.testname`` file written by ``run_utest.py``.  When found, its
    content (the test binary basename) is attached to every report object via
    ``setattr(report, "test_name", ...)``.  If absent, ``test_name`` is set to
    the empty string.
    """
    reports = []
    pattern = re.compile(rf"^{re.escape(prefix)}\.")
    for entry in sorted(report_dir.iterdir()):
        if not pattern.match(entry.name):
            continue
        if entry.suffix in (".sarif", ".md", ".testname"):
            continue
        result = parse_fn(entry)
        if result is None:
            continue
        # Attach the test binary name from the companion file (best-effort).
        testname_file = entry.parent / (entry.name + ".testname")
        test_name = testname_file.read_text(encoding="utf-8").strip() \
            if testname_file.exists() else ""
        items = result if isinstance(result, list) else [result]
        for r in items:
            setattr(r, "test_name", test_name)
        if isinstance(result, list):
            reports.extend(result)
        else:
            reports.append(result)
    return reports


# ── Source-path resolution ────────────────────────────────────────────────────

def resolve_frame_path(frame: StackFrame, source_root: Path) -> None:
    """Populate *frame.rel_file* relative to *source_root*."""
    if not frame.file:
        return
    try:
        frame.rel_file = str(Path(frame.file).relative_to(source_root))
    except ValueError:
        frame.rel_file = frame.file  # outside checkout — keep as-is


def resolve_paths_frames(reports: list, source_root: Path) -> None:
    """Resolve rel_file for every frame in ``report.frames``.

    Suitable for ASan reports where each report has a plain ``frames`` list.
    For UBSan (which also has a top-level ``file``/``rel_file`` pair),
    use a wrapper that also calls ``resolve_frame_path`` on the report itself.
    """
    for report in reports:
        for frame in report.frames:
            resolve_frame_path(frame, source_root)


def resolve_paths_threaded(reports: list, source_root: Path) -> None:
    """Resolve rel_file for TSan reports whose frames are nested inside threads."""
    for report in reports:
        for thread in report.threads:
            for frame in thread.frames:
                resolve_frame_path(frame, source_root)


# ── SARIF helpers ─────────────────────────────────────────────────────────────

def sarif_location(rel_file: Optional[str], line: Optional[int],
                   col: Optional[int] = None) -> dict:
    """Build a SARIF ``location`` dict for a given source position."""
    if not rel_file:
        return {}
    loc: dict = {
        "physicalLocation": {
            "artifactLocation": {"uri": rel_file, "uriBaseId": "%SRCROOT%"},
        }
    }
    if line:
        loc["physicalLocation"]["region"] = {
            "startLine": line,
            "startColumn": col or 1,
        }
    return loc


def build_sarif_doc(tool_name: str, tool_uri: str,
                    rules: list, results: list) -> dict:
    """Assemble a complete SARIF 2.1.0 document."""
    return {
        "$schema": _SARIF_SCHEMA,
        "version": _SARIF_VERSION,
        "runs": [
            {
                "tool": {
                    "driver": {
                        "name": tool_name,
                        "version": _TOOL_VERSION,
                        "informationUri": tool_uri,
                        "rules": rules,
                    }
                },
                "results": results,
            }
        ],
    }


# ── Markdown summary template ─────────────────────────────────────────────────

def build_summary_md(
        tool_name: str,
        emoji: str,
        items: list,
        headers: list,
        row_fn: Callable,
        details_fn: Callable,
        note: str = "",
        no_items_msg: str = "") -> str:
    """Build a unified Markdown summary for any sanitizer parser.

    All three parsers (ASan, UBSan, TSan) share the same output structure:
    a short summary table followed by a collapsible details section that
    contains the full raw report for each finding.

    Args:
        tool_name:    Human-readable tool name, e.g. ``"AddressSanitizer"``.
        emoji:        Status emoji: ``"❌"`` when issues found, ``"✅"`` otherwise.
        items:        List of items to render; may be plain reports or any objects
                      accepted by ``row_fn`` / ``details_fn`` (e.g. deduplicated
                      ``(report, count)`` tuples for UBSan).
        headers:      Column names for the summary table.
        row_fn:       ``(index: int, item) → list[str]`` — returns the table cell
                      values (already Markdown-formatted) for one row.
        details_fn:   ``(index: int, item) → list[str]`` — returns the Markdown
                      lines that make up the ``#### Report N`` details block.
        note:         Optional Markdown note displayed above the summary table
                      (e.g. the Argobots false-positive warning for TSan).
        no_items_msg: Message to return when ``items`` is empty.  Defaults to a
                      standard "no issues" line for *tool_name*.
    """
    if not items:
        msg = no_items_msg or f"#### ✅ {tool_name} — No issues detected"
        return msg + "\n"

    lines = [f"#### {emoji} {tool_name} — {len(items)} issue(s) detected", ""]

    if note:
        lines += [note, ""]

    # Summary table
    header_row = " | ".join(headers)
    sep_row = " | ".join(["---"] * len(headers))
    lines += [f"| {header_row} |", f"| {sep_row} |"]
    for i, item in enumerate(items, 1):
        cells = row_fn(i, item)
        lines.append("| " + " | ".join(cells) + " |")

    # Collapsible details section
    lines += [
        "",
        "<details>",
        f"<summary>Full {tool_name} reports</summary>",
        "",
    ]
    for i, item in enumerate(items, 1):
        lines += details_fn(i, item)
    lines += ["</details>", ""]

    return "\n".join(lines) + "\n"


# ── Shared "#### Report N" details block ──────────────────────────────────────

def report_heading(index: int, report) -> list:
    """Return the Markdown lines for one report's details block.

    Used directly as the ``details_fn`` argument to ``build_summary_md()`` by
    all three parsers: a heading naming the error type, test, and pid,
    followed by the raw captured log text in a fenced block. This is the
    entire details block for every finding - a prior "call chain" table
    reformatting the same file/line/function information immediately above
    was removed as redundant (raw sanitizer output is already readable, and
    the summary table above already surfaces the one primary location).

    *report* only needs ``error_type``, ``test_name``, ``pid``, and ``raw``
    attributes, which AsanReport, UbsanReport, and TsanReport all provide.
    """
    return [
        f"#### Report {index} — `{report.error_type}` in "
        f"**{report.test_name or 'unknown'}** (pid {report.pid})",
        "",
        "```text",
        report.raw.strip(),
        "```",
        "",
    ]


# ── CLI helpers ───────────────────────────────────────────────────────────────

def get_args(description: str) -> argparse.Namespace:
    """Parse the standard four CLI arguments shared by all sanitizer parsers."""
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--report-dir", required=True,
                        help="Directory containing <sanitizer>.<pid> log files")
    parser.add_argument("--source-root", required=True,
                        help="Absolute path to the repository checkout root")
    parser.add_argument("--sarif-out", required=True,
                        help="Output path for the SARIF 2.1.0 file")
    parser.add_argument("--summary-out", required=True,
                        help="Output path for the Markdown summary file")
    return parser.parse_args()


def main_runner(args: argparse.Namespace,
                collect_fn: Callable,
                resolve_fn: Callable,
                build_sarif_fn: Callable,
                build_summary_fn: Callable,
                tool_label: str) -> int:
    """Shared control flow for all sanitizer parsers.

    Returns 1 when violations are found (non-zero exit signals GHA step
    failure), 0 when the report directory is absent or empty.
    """
    report_dir = Path(args.report_dir).resolve()
    source_root = Path(args.source_root).resolve()
    sarif_out = Path(args.sarif_out)
    summary_out = Path(args.summary_out)

    if not report_dir.is_dir():
        print(f"::warning::{tool_label} log directory not found: {report_dir}")
        sarif_out.write_text(json.dumps(build_sarif_fn([]), indent=2),
                             encoding="utf-8")
        return 0

    reports = collect_fn(report_dir)
    resolve_fn(reports, source_root)
    print(f"Parsed {len(reports)} {tool_label} report(s) from {report_dir}")

    if not reports:
        print(f"No {tool_label} violations found.")

    sarif_out.write_text(json.dumps(build_sarif_fn(reports), indent=2),
                         encoding="utf-8")
    summary_out.write_text(build_summary_fn(reports), encoding="utf-8")
    return 1 if reports else 0
