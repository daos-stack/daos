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
from pathlib import Path

from sanitizer_report_base import (ADDR_FRAME_RE, StackFrame, build_sarif_doc, build_summary_md,
                                   collect_reports, get_args, main_runner, report_heading,
                                   resolve_paths_frames, sarif_location)

# -- Data structures -----------------------------------------------------------


class UbsanReport:
    # pylint: disable=too-many-instance-attributes,too-few-public-methods
    """One UBSan violation report parsed from a ubsan.<pid> log file."""

    def __init__(self, pid, error_type, error_summary, file, line, column, rel_file,
                 frames=None, raw="", test_name=""):
        self.pid = pid
        self.error_type = error_type        # e.g. "signed-integer-overflow"
        self.error_summary = error_summary  # the full "runtime error: ..." line
        self.file = file                    # source file where the violation occurred
        self.line = line
        self.column = column
        self.rel_file = rel_file            # relative path, filled in later
        self.frames = frames if frames is not None else []  # list[StackFrame]
        self.raw = raw
        self.test_name = test_name          # suite name, filled in by collect_reports()


# -- Parsing -------------------------------------------------------------------

# Matches the primary violation line:
#   /path/file.c:42:13: runtime error: signed integer overflow: ...
_VIOLATION_RE = re.compile(
    r"^(?P<file>[^:]+\.(?:c|cc|cpp|cxx|h|hpp)):(?P<line>\d+):(?P<col>\d+):"
    r"\s+runtime error:\s+(?P<msg>.+)$"
)

# Fallback: SUMMARY line written when the process is aborted before the full
# report can be flushed (e.g., ASan kills the process first).
# Format: SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior <file>:<line>:<col> in
_SUMMARY_RE = re.compile(
    r"^SUMMARY: UndefinedBehaviorSanitizer: (?P<type>\S+(?:-\S+)*)"
    r"(?: (?P<file>[^:\s]+\.(?:c|cc|cpp|cxx|h|hpp)):(?P<line>\d+):(?P<col>\d+))?"
)

# Matches UBSan stack frames (same format as ASan when print_stacktrace=1):
#   #0 0xADDR in function_name /path/file.c:line[:col]
# Shared with ASan via ADDR_FRAME_RE.


def _normalize_error_type(msg):
    """Convert a 'runtime error: ...' message to a kebab-case type tag."""
    msg = msg.lower().split(":")[0].strip()
    msg = re.sub(r"[^a-z0-9]+", "-", msg).strip("-")
    if msg:
        return msg
    return "undefined-behavior"


def parse_report_file(path):
    """Parse one UBSan log file. Returns a list of UbsanReport objects."""
    text = path.read_text(errors="replace")
    pid = re.sub(r"^.*\.", "", path.name)  # ubsan.12345 -> "12345"
    reports = []
    current_report = None

    for line in text.splitlines():
        violation_match = _VIOLATION_RE.match(line)
        if violation_match:
            # Commit any in-progress report before starting a new one
            if current_report is not None:
                reports.append(current_report)
            current_report = UbsanReport(
                pid=pid,
                error_type=_normalize_error_type(violation_match.group("msg")),
                error_summary=line.strip(),
                file=violation_match.group("file"),
                line=int(violation_match.group("line")),
                column=int(violation_match.group("col")),
                rel_file=None,
                frames=[],
                raw=line + "\n",
            )
            continue

        if current_report is not None:
            current_report.raw += line + "\n"
            frame_match = ADDR_FRAME_RE.match(line)
            if frame_match:
                current_report.frames.append(StackFrame(
                    index=int(frame_match.group("idx")),
                    function=frame_match.group("func"),
                    file=frame_match.group("file"),
                    line=int(frame_match.group("line")) if frame_match.group("line") else None,
                    column=int(frame_match.group("col")) if frame_match.group("col") else None,
                    rel_file=None,
                ))
            continue

        # Fallback: process was aborted before the full report was written;
        # only the SUMMARY line is available.  Create a minimal report from it.
        summary_match = _SUMMARY_RE.match(line)
        if summary_match and current_report is None:
            summary_file = summary_match.group("file")
            summary_line = int(summary_match.group("line")) if summary_match.group("line") else None
            summary_col = int(summary_match.group("col")) if summary_match.group("col") else None
            reports.append(UbsanReport(
                pid=pid,
                error_type=summary_match.group("type"),
                error_summary=f"runtime error: {summary_match.group('type').replace('-', ' ')}"
                              + (f" at {summary_file}:{summary_line}" if summary_file else ""),
                file=summary_file,
                line=summary_line,
                column=summary_col,
                rel_file=None,
                frames=[],
                raw=line + "\n",
            ))

    if current_report is not None:
        reports.append(current_report)

    return reports


def _collect_reports(report_dir):
    """Return parsed UbsanReport objects for every ubsan.<pid> file found."""
    return collect_reports(report_dir, "ubsan", parse_report_file)


def resolve_paths(reports, source_root):
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
                report.rel_file = report.file  # outside checkout -- keep as-is


# -- SARIF 2.1.0 ---------------------------------------------------------------

_TOOL_NAME = "UndefinedBehaviorSanitizer"
_TOOL_URI = "https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html"


def build_sarif(reports):
    """Build a SARIF 2.1.0 document from the list of UbsanReport objects."""
    rules = {}
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
        primary_loc = sarif_location(report.rel_file, report.line, report.column)
        if primary_loc:
            locations.append(primary_loc)

        # Fallback: first in-project frame
        if not locations:
            in_project = [f for f in report.frames if f.rel_file and f.line]
            if in_project:
                frame = in_project[0]
                locations.append(sarif_location(frame.rel_file, frame.line, frame.column))

        results.append({
            "ruleId": rule_id,
            "level": "error",
            "message": {"text": report.error_summary},
            "locations": locations,
        })

    return build_sarif_doc(_TOOL_NAME, _TOOL_URI, list(rules.values()), results)


# -- Markdown summary ----------------------------------------------------------


def _ubsan_row(idx, report):
    """Return summary table cells for one UBSan report."""
    loc = f"`{report.rel_file or report.file or 'unknown'}:{report.line or '?'}`"
    desc = report.error_summary[:80]
    test_name = "_(unknown)_"
    if report.test_name:
        test_name = report.test_name
    return [str(idx), test_name, f"`{report.error_type}`", loc, desc]


def build_summary(reports):
    """Return a Markdown summary block for the GitHub job summary.

    With halt_on_error=1 each ubsan.<pid> file holds at most one violation,
    but the same bug in shared library code can be triggered by multiple test
    binaries (each producing its own file).  Deduplicate by
    (error_type, file, line) in the summary table so the same location is not
    listed repeatedly; individual detail blocks are still shown for every report.
    """
    # Deduplicate for the summary table only (details show all reports).
    seen = set()
    unique = []
    for report in reports:
        key = (report.error_type, report.rel_file or report.file, report.line)
        if key not in seen:
            seen.add(key)
            unique.append(report)
    return build_summary_md(
        tool_name="UndefinedBehaviorSanitizer",
        emoji="FAIL",
        items=unique,
        headers=["#", "Test", "Error type", "Location", "Description"],
        row_fn=_ubsan_row,
        details_fn=report_heading,
        no_items_msg="#### PASS UndefinedBehaviorSanitizer -- "
                     "No undefined-behavior issues detected",
    )


# -- Main ----------------------------------------------------------------------

def main():
    """Entry point."""
    return main_runner(
        get_args("Parse UBSan log files and emit SARIF and a summary."),
        collect_fn=_collect_reports,
        resolve_fn=resolve_paths,
        build_sarif_fn=build_sarif,
        build_summary_fn=build_summary,
        tool_label="UBSan",
    )


if __name__ == "__main__":
    sys.exit(main())
