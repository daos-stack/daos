#!/usr/bin/env python3
"""
  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
  All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

  Parse UndefinedBehaviorSanitizer log files produced during CI unit test runs.

  UBSan report format (one violation per line or block):
    /absolute/path/file.c:42:13: runtime error: signed integer overflow: ...
    /absolute/path/file.c:42:13: runtime error: null pointer passed as argument...
    /absolute/path/file.c:42:13: runtime error: load of misaligned address...

  When UBSAN_OPTIONS includes print_stacktrace=1, each violation is followed by
  a stack trace in the same format as ASan:
    #0 0xADDR in function_name /path/file.c:line

  Outputs:
    * SARIF 2.1.0 file  -> uploaded to the Security / Code scanning tab
    * Markdown summary  -> appended to $GITHUB_STEP_SUMMARY

  Invocation (from .github/workflows/unit-test-template.yml, called by unit-testing.yml):

    python3 utils/ci/parse_ubsan_reports.py \
        --report-dir  sanitizer-logs           \
        --source-root "$(pwd)"                \
        --sarif-out   test-results/ubsan.sarif \
        --summary-out test-results/ubsan_summary.md

  UBSan must be started with:
    UBSAN_OPTIONS="log_path=<dir>/ubsan:exitcode=42:print_stacktrace=1"
  so that each process writes its own ubsan.<pid> file into the shared
  sanitizer-logs/ directory.
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
class UbsanReport:  # pylint: disable=too-many-instance-attributes
    """One UBSan violation report parsed from a ubsan.<pid> log file."""

    pid: str
    error_type: str          # e.g. "signed-integer-overflow"
    error_summary: str       # the full "runtime error: ..." line
    file: Optional[str]      # source file where the violation occurred
    line: Optional[int]
    column: Optional[int]
    rel_file: Optional[str]  # relative path, filled in later
    frames: list = field(default_factory=list)  # list[StackFrame]
    raw: str = ""
    test_name: str = ""      # suite name, filled in by collect_reports()


# ── Parsing ───────────────────────────────────────────────────────────────────

# Matches the primary violation line:
#   /path/file.c:42:13: runtime error: signed integer overflow: ...
_RE_VIOLATION = re.compile(
    r"^(?P<file>[^:]+\.(?:c|cc|cpp|cxx|h|hpp)):(?P<line>\d+):(?P<col>\d+):"
    r"\s+runtime error:\s+(?P<msg>.+)$"
)

# Fallback: SUMMARY line written when the process is aborted before the full
# report can be flushed (e.g., ASan kills the process first).
# Format: SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior <file>:<line>:<col> in
_RE_SUMMARY = re.compile(
    r"^SUMMARY: UndefinedBehaviorSanitizer: (?P<type>\S+(?:-\S+)*)"
    r"(?: (?P<file>[^:\s]+\.(?:c|cc|cpp|cxx|h|hpp)):(?P<line>\d+):(?P<col>\d+))?"
)

# Matches UBSan stack frames (same format as ASan when print_stacktrace=1):
#   #0 0xADDR in function_name /path/file.c:line[:col]
# Shared with ASan via ADDR_FRAME_RE, imported above as _RE_FRAME.


def _normalize_error_type(msg: str) -> str:
    """Convert a 'runtime error: ...' message to a kebab-case type tag."""
    msg = msg.lower().split(":")[0].strip()
    msg = re.sub(r"[^a-z0-9]+", "-", msg).strip("-")
    return msg or "undefined-behavior"


def parse_report_file(path: Path) -> list:
    """Parse one UBSan log file. Returns a list of UbsanReport objects."""
    text = path.read_text(errors="replace")
    pid = re.sub(r"^.*\.", "", path.name)  # ubsan.12345 → "12345"
    reports = []
    current: Optional[UbsanReport] = None

    for line in text.splitlines():
        m = _RE_VIOLATION.match(line)
        if m:
            # Commit any in-progress report before starting a new one
            if current is not None:
                reports.append(current)
            current = UbsanReport(
                pid=pid,
                error_type=_normalize_error_type(m.group("msg")),
                error_summary=line.strip(),
                file=m.group("file"),
                line=int(m.group("line")),
                column=int(m.group("col")),
                rel_file=None,
                frames=[],
                raw=line + "\n",
            )
            continue

        if current is not None:
            current.raw += line + "\n"
            m = _RE_FRAME.match(line)
            if m:
                current.frames.append(StackFrame(
                    index=int(m.group("idx")),
                    function=m.group("func"),
                    file=m.group("file"),
                    line=int(m.group("line")) if m.group("line") else None,
                    column=int(m.group("col")) if m.group("col") else None,
                    rel_file=None,
                ))
            continue

        # Fallback: process was aborted before the full report was written;
        # only the SUMMARY line is available.  Create a minimal report from it.
        m = _RE_SUMMARY.match(line)
        if m and current is None:
            summary_file = m.group("file")
            summary_line = int(m.group("line")) if m.group("line") else None
            summary_col = int(m.group("col")) if m.group("col") else None
            reports.append(UbsanReport(
                pid=pid,
                error_type=m.group("type"),
                error_summary=f"runtime error: {m.group('type').replace('-', ' ')}"
                              + (f" at {summary_file}:{summary_line}" if summary_file else ""),
                file=summary_file,
                line=summary_line,
                column=summary_col,
                rel_file=None,
                frames=[],
                raw=line + "\n",
            ))

    if current is not None:
        reports.append(current)

    return reports


def collect_reports(report_dir: Path) -> list:
    """Return parsed UbsanReport objects for every ubsan.<pid> file found."""
    return _collect_reports(report_dir, "ubsan", parse_report_file)


def resolve_paths(reports: list, source_root: Path) -> None:
    """Resolve source-relative paths for UBSan reports.

    UBSan reports have two path locations to resolve:
    - ``report.frames``: stack frames (same as ASan) via resolve_paths_frames
    - ``report.file`` / ``report.rel_file``: the top-level violation site, which
      is unique to UBSan and not present on ASan or TSan reports.
    """
    resolve_paths_frames(reports, source_root)
    for report in reports:
        if report.file and report.rel_file is None:
            try:
                report.rel_file = str(Path(report.file).relative_to(source_root))
            except ValueError:
                report.rel_file = report.file  # outside checkout — keep as-is


# ── SARIF 2.1.0 ───────────────────────────────────────────────────────────────

_TOOL_NAME = "UndefinedBehaviorSanitizer"
_TOOL_URI = "https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html"


def build_sarif(reports: list) -> dict:
    """Build a SARIF 2.1.0 document from the list of UbsanReport objects."""
    rules: dict = {}
    results = []

    for report in reports:
        rule_id = f"ubsan/{report.error_type}"
        if rule_id not in rules:
            rules[rule_id] = {
                "id": rule_id,
                "name": "".join(
                    w.capitalize() for w in report.error_type.split("-")
                ),
                "shortDescription": {"text": report.error_type.replace("-", " ")},
                "helpUri": _TOOL_URI,
                "properties": {"tags": ["ubsan", "undefined-behavior"]},
            }

        # Primary location: the violation site
        locations = []
        primary_loc = _sarif_location_base(report.rel_file, report.line, report.column)
        if primary_loc:
            locations.append(primary_loc)

        # Fallback: first in-project frame
        if not locations:
            in_proj = [f for f in report.frames if f.rel_file and f.line]
            if in_proj:
                frm = in_proj[0]
                locations.append(_sarif_location_base(frm.rel_file, frm.line, frm.column))

        results.append({
            "ruleId": rule_id,
            "level": "error",
            "message": {"text": report.error_summary},
            "locations": locations,
        })

    return build_sarif_doc(_TOOL_NAME, _TOOL_URI, list(rules.values()), results)


# ── Markdown summary ──────────────────────────────────────────────────────────


def _ubsan_row(i: int, report) -> list:
    """Return summary table cells for one UBSan report."""
    loc = f"`{report.rel_file or report.file or 'unknown'}:{report.line or '?'}`"
    desc = report.error_summary[:80]
    test_name = report.test_name or "_(unknown)_"
    return [str(i), test_name, f"`{report.error_type}`", loc, desc]


def build_summary(reports: list) -> str:
    """Return a Markdown summary block for the GitHub job summary.

    With halt_on_error=1 each ubsan.<pid> file holds at most one violation,
    but the same bug in shared library code can be triggered by multiple test
    binaries (each producing its own file).  Deduplicate by
    (error_type, file, line) in the summary table so the same location is not
    listed repeatedly; individual detail blocks are still shown for every report.
    """
    if not reports:
        return ("### ✅ UndefinedBehaviorSanitizer"
                " — No undefined-behavior issues detected\n")
    # Deduplicate for the summary table only (details show all reports).
    seen: set = set()
    unique: list = []
    for r in reports:
        key = (r.error_type, r.rel_file or r.file, r.line)
        if key not in seen:
            seen.add(key)
            unique.append(r)
    return build_summary_md(
        tool_name="UndefinedBehaviorSanitizer",
        emoji="❌",
        items=unique,
        headers=["#", "Test", "Error type", "Location", "Description"],
        row_fn=_ubsan_row,
        details_fn=report_heading,
        no_items_msg=("#### ✅ UndefinedBehaviorSanitizer"
                      " — No undefined-behavior issues detected"),
    )


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> int:
    """Entry point."""
    return main_runner(
        get_args("Parse UBSan log files and emit SARIF and a summary."),
        collect_fn=collect_reports,
        resolve_fn=resolve_paths,
        build_sarif_fn=build_sarif,
        build_summary_fn=build_summary,
        tool_label="UBSan",
    )


if __name__ == "__main__":
    sys.exit(main())
