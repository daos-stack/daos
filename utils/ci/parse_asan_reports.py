#!/usr/bin/env python3
"""
  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
  All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

  Parse AddressSanitizer log files produced during CI unit test runs.

  Outputs:
    • SARIF 2.1.0 file  → uploaded to the Security / Code scanning tab
    • Markdown summary  → appended to $GITHUB_STEP_SUMMARY

  Invocation (from .github/workflows/unit-test-template.yml, called by unit-testing.yml):

    python3 utils/ci/parse_asan_reports.py \
        --report-dir  sanitizer-logs          \
        --source-root "$(pwd)"                \
        --sarif-out   test-results/asan.sarif \
        --summary-out test-results/asan_summary.md

  ASan must be started with:
    ASAN_OPTIONS="log_path=<dir>/asan:exitcode=42:symbolize=1"
  so that each process writes its own asan.<pid> file into the shared
  sanitizer-logs/ directory and the stack frames already contain resolved
  function / file / line information.
"""

import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from sanitizer_report_base import ADDR_FRAME_RE as _RE_FRAME
from sanitizer_report_base import StackFrame, build_sarif_doc, build_summary_md
from sanitizer_report_base import collect_reports as _collect_reports
from sanitizer_report_base import get_args, main_runner, report_heading, resolve_paths_frames
from sanitizer_report_base import sarif_location as _sarif_location_base

# ── Data structures ───────────────────────────────────────────────────────────


@dataclass
class AsanReport:  # pylint: disable=too-many-instance-attributes
    """One ASan error report parsed from a single asan.<pid> log file."""

    pid: str
    error_type: str          # e.g. "stack-buffer-overflow"
    error_summary: str       # the first ==PID==ERROR: … line
    access_type: str         # "READ" | "WRITE" | "unknown"
    access_size: Optional[int]
    frames: list = field(default_factory=list)  # list[StackFrame]
    raw: str = ""
    test_name: str = ""      # suite name, filled in by collect_reports()


# ── Parsing ───────────────────────────────────────────────────────────────────

_RE_ERROR_HEADER = re.compile(
    r"==\d+==ERROR: AddressSanitizer: (?P<type>\S+(?:\s+\S+)*?) on address"
)
_RE_ACCESS = re.compile(r"(?P<access>READ|WRITE) of size (?P<size>\d+)")


def parse_report_file(path: Path) -> Optional[AsanReport]:
    """Parse one ASan log file. Returns None when it contains no error."""
    text = path.read_text(errors="replace")
    if "AddressSanitizer" not in text:
        return None

    pid = re.sub(r"^.*\.", "", path.name)  # asan.12345 → "12345"
    error_type = "unknown"
    error_summary = ""
    access_type = "unknown"
    access_size = None
    frames = []

    for line in text.splitlines():
        m = _RE_ERROR_HEADER.search(line)
        if m:
            error_type = m.group("type").strip()
            error_summary = line.strip()
            continue

        m = _RE_ACCESS.search(line)
        if m and access_type == "unknown":
            access_type = m.group("access")
            access_size = int(m.group("size"))
            continue

        m = _RE_FRAME.match(line)
        if m:
            frames.append(StackFrame(
                index=int(m.group("idx")),
                function=m.group("func"),
                file=m.group("file"),
                line=int(m.group("line")) if m.group("line") else None,
                column=int(m.group("col")) if m.group("col") else None,
                rel_file=None,
            ))

    if error_type == "unknown" and not frames:
        return None

    return AsanReport(
        pid=pid,
        error_type=error_type,
        error_summary=error_summary,
        access_type=access_type,
        access_size=access_size,
        frames=frames,
        raw=text,
    )


def collect_reports(report_dir: Path) -> list:
    """Return parsed AsanReport objects for every asan.<pid> file found."""
    return _collect_reports(report_dir, "asan", parse_report_file)


# ── SARIF 2.1.0 ───────────────────────────────────────────────────────────────

_TOOL_URI = "https://clang.llvm.org/docs/AddressSanitizer.html"


def _sarif_location(frame: StackFrame) -> dict:
    """Build a SARIF location with optional logicalLocations for function name."""
    loc = _sarif_location_base(frame.rel_file, frame.line, frame.column)
    if loc and frame.function:
        loc["logicalLocations"] = [{"name": frame.function, "kind": "function"}]
    # Fallback: use raw file path if rel_file not resolved
    if not loc and frame.file:
        loc = {
            "physicalLocation": {
                "artifactLocation": {"uri": frame.file, "uriBaseId": "%SRCROOT%"}
            }
        }
    return loc or {}


def build_sarif(reports: list) -> dict:
    """Build a SARIF 2.1.0 document from the parsed ASan reports."""
    rule_ids = sorted({r.error_type for r in reports})
    rules = [
        {
            "id": rid,
            "name": rid.replace("-", " ").title().replace(" ", ""),
            "shortDescription": {"text": f"AddressSanitizer: {rid}"},
            "helpUri": _TOOL_URI,
            "properties": {"tags": ["security", "correctness", "memory"]},
        }
        for rid in rule_ids
    ]

    results = []
    for report in reports:
        in_project = [f for f in report.frames if f.rel_file and f.line]
        all_framed = [f for f in report.frames if f.file]
        primary = (in_project or all_framed or [None])[0]

        if primary is None:
            # No usable frame — skip: SARIF requires a physicalLocation.
            continue

        primary_loc = _sarif_location(primary)
        if not primary_loc:
            # Frame has neither a project-relative path nor a raw file path.
            continue

        thread_flow_locs = [
            {
                "location": _sarif_location(f),
                "nestingLevel": f.index,
                "executionOrder": f.index,
            }
            for f in report.frames if f.file
        ]

        code_flows = []
        if thread_flow_locs:
            code_flows = [{
                "message": {"text": f"ASan call stack (pid {report.pid})"},
                "threadFlows": [{"locations": thread_flow_locs}],
            }]

        msg = (
            f"{report.access_type} of size {report.access_size} bytes"
            f" detected by AddressSanitizer ({report.error_type}).\n\n"
            f"```\n{report.error_summary}\n```"
        )
        result: dict = {
            "ruleId": report.error_type,
            "level": "error",
            "message": {"text": msg},
            "locations": [primary_loc],
        }
        if code_flows:
            result["codeFlows"] = code_flows
        results.append(result)

    return build_sarif_doc("AddressSanitizer", _TOOL_URI, rules, results)


# ── Markdown job summary ──────────────────────────────────────────────────────

def _asan_row(i: int, report) -> list:
    """Return summary table cell values for one ASan report."""
    in_proj = [f for f in report.frames if f.rel_file and f.line]
    if in_proj:
        p = in_proj[0]
        loc = f"`{p.rel_file}:{p.line}`"
        func = f"`{p.function}()`"
    else:
        loc, func = "_(no source)_", "_(unknown)_"
    test_name = report.test_name or "_(unknown)_"
    return [
        str(i),
        test_name,
        f"`{report.error_type}`",
        report.access_type,
        f"{report.access_size or '?'} B",
        loc,
        func,
    ]


def build_summary(reports: list) -> str:
    """Return a GitHub-flavoured Markdown summary of all ASan findings."""
    return build_summary_md(
        tool_name="AddressSanitizer",
        emoji="❌",
        items=reports,
        headers=["#", "Test", "Error type", "Access", "Size", "Primary location", "Function"],
        row_fn=_asan_row,
        details_fn=report_heading,
        no_items_msg="#### ✅ AddressSanitizer — No memory-safety issues detected",
    )

# ── CLI ───────────────────────────────────────────────────────────────────────


def main() -> int:
    """Entry point."""
    return main_runner(
        get_args("Parse ASan logs → SARIF + summary"),
        collect_fn=collect_reports,
        resolve_fn=resolve_paths_frames,
        build_sarif_fn=build_sarif,
        build_summary_fn=build_summary,
        tool_label="ASan",
    )


if __name__ == "__main__":
    sys.exit(main())
