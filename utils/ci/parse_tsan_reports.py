#!/usr/bin/env python3
"""
  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
  All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

  Parse ThreadSanitizer log files produced during CI unit test runs.

  TSan report format:
    WARNING: ThreadSanitizer: data race (pid=12345)
      Write of size 4 at 0xADDR by thread T1:
        #0 func_name /path/file.c:42 (binary+0xaddr)
      Previous read of size 4 at 0xADDR by thread T2:
        #0 other_func /path/file.c:7 (binary+0xaddr)
    SUMMARY: ThreadSanitizer: data race ...

  Outputs:
    * SARIF 2.1.0 file  -> uploaded to the Security / Code scanning tab
    * Markdown summary  -> appended to $GITHUB_STEP_SUMMARY

  Invocation (from .github/workflows/unit-test-template.yml, called by unit-testing.yml):

    python3 utils/ci/parse_tsan_reports.py \
        --report-dir  sanitizer-logs          \
        --source-root "$(pwd)"                \
        --sarif-out   test-results/tsan.sarif \
        --summary-out test-results/tsan_summary.md

  TSan must be started with:
    TSAN_OPTIONS="log_path=<dir>/tsan:exitcode=42:second_deadlock_stack=1"
  so that each process writes its own tsan.<pid> file into the shared
  sanitizer-logs/ directory.
"""

import re
import sys

from sanitizer_report_base import (FILE_LINE_COL, StackFrame, build_sarif_doc, build_summary_md,
                                   collect_reports, get_args, main_runner, report_heading,
                                   resolve_paths_threaded, sarif_location)

# -- Data structures -----------------------------------------------------------


class TsanThread:
    # pylint: disable=too-few-public-methods
    """One stack (write/read/mutex thread) within a TSan report.

    Only the frames are needed: the descriptive role line ("Write of size 4
    at 0xADDR by thread T1:", etc.) is already preserved verbatim in the
    report's own `raw` text, which is what gets rendered.
    """

    def __init__(self, frames=None):
        self.frames = frames if frames is not None else []  # list of StackFrame


class TsanReport:
    # pylint: disable=too-few-public-methods
    """One TSan error report (race, deadlock, etc.) parsed from a tsan.<pid> log."""

    def __init__(self, pid, error_type, error_summary, threads, raw="", test_name=""):
        self.pid = pid
        self.error_type = error_type        # e.g. "data-race", "lock-order-inversion", ...
        self.error_summary = error_summary  # the full WARNING: ... line
        self.threads = threads              # list of TsanThread
        self.raw = raw
        self.test_name = test_name  # suite name, filled in by collect_reports()


# -- Parsing -------------------------------------------------------------------

# Matches the header line of a TSan report:
#   WARNING: ThreadSanitizer: data race (pid=12345)
_HEADER_RE = re.compile(
    r"WARNING: ThreadSanitizer:\s+(?P<type>.+?)\s+\(pid=(?P<pid>\d+)\)"
)

# Matches a thread role descriptor line:
# - Write of size 4 at 0xADDR by thread T1:
# - Previous read of size 4 at 0xADDR by main thread:
# - Mutex M1 ... created by main thread:
_THREAD_ROLE_RE = re.compile(
    r"^  (?:(?:Previous|Subsequent)\s+)?(?:Write|Read|Mutex|Lock)\s+",
    re.IGNORECASE,
)

# Matches TSan stack frames:
#   #0 func_name /path/file.c:42 (binary+0xaddr)
#   #0 func_name /path/file.c:42:7 (binary+0xaddr)
# Lacks the "0xADDR" address ASan/UBSan have and has a trailing "(binary+off)",
# so it composes its own regex from the shared FILE_LINE_COL fragment rather
# than reusing ADDR_FRAME_RE.
_FRAME_RE = re.compile(
    rf"^\s+#(?P<idx>\d+)\s+(?P<func>\S+)"
    rf"(?:\s+{FILE_LINE_COL})?"
    rf"(?:\s+\([^)]+\))?$",
    re.IGNORECASE,
)


def _normalize_error_type(raw):
    """Convert 'data race' -> 'data-race', etc."""
    error_type = re.sub(r"[^a-z0-9]+", "-", raw.lower().strip()).strip("-")
    if error_type:
        return error_type
    return "race"


def parse_report_file(path):
    """Parse one TSan log file. Returns a list of TsanReport objects."""
    text = path.read_text(errors="replace")
    if "ThreadSanitizer" not in text:
        return []

    reports = []
    current_report = None
    current_thread = None

    for line in text.splitlines():
        header_match = _HEADER_RE.match(line)
        if header_match:
            # Commit current thread
            if current_thread is not None and current_report is not None:
                current_report.threads.append(current_thread)
                current_thread = None

            # Commit current report
            if current_report is not None:
                reports.append(current_report)

            current_report = TsanReport(
                pid=header_match.group("pid"),
                error_type=_normalize_error_type(header_match.group("type")),
                error_summary=line.strip(),
                threads=[],
                raw=line + "\n",
            )
            continue

        if current_report is None:
            continue
        # Commit the line to the current report's raw text
        current_report.raw += line + "\n"

        # Detect thread role lines
        if _THREAD_ROLE_RE.match(line):
            # Commit current thread and start a new one
            if current_thread is not None:
                current_report.threads.append(current_thread)
            current_thread = TsanThread(frames=[])
            continue

        # Stack frames
        frame_match = _FRAME_RE.match(line)
        if frame_match and current_thread is not None:
            current_thread.frames.append(StackFrame(
                index=int(frame_match.group("idx")),
                function=frame_match.group("func"),
                file=frame_match.group("file"),
                line=int(frame_match.group("line")) if frame_match.group("line") else None,
                column=int(frame_match.group("col")) if frame_match.group("col") else None,
                rel_file=None,
            ))

    # Commit any in-progress report
    if current_thread is not None and current_report is not None:
        current_report.threads.append(current_thread)
    if current_report is not None:
        reports.append(current_report)

    return reports


def _collect_reports(report_dir):
    """Return parsed TsanReport objects for every tsan.<pid> file found."""
    return collect_reports(report_dir, "tsan", parse_report_file)


def _primary_frame(report):
    """Return the innermost in-project frame from the first thread stack."""
    for thread in report.threads:
        for frame in thread.frames:
            if frame.rel_file and frame.line:
                return frame
    return None


# -- SARIF 2.1.0 ---------------------------------------------------------------

_TOOL_NAME = "ThreadSanitizer"
_TOOL_URI = "https://clang.llvm.org/docs/ThreadSanitizer.html"


def build_sarif(reports):
    """Build a SARIF 2.1.0 document from the list of TsanReport objects."""
    rules = {}
    results = []

    for report in reports:
        rule_id = f"tsan/{report.error_type}"
        if rule_id not in rules:
            rules[rule_id] = {
                "id": rule_id,
                "name": "".join(
                    w.capitalize() for w in report.error_type.split("-")
                ),
                "shortDescription": {"text": report.error_type.replace("-", " ")},
                "helpUri": _TOOL_URI,
                "properties": {"tags": ["tsan", "thread-safety"]},
            }

        primary = _primary_frame(report)
        locations = []
        if primary:
            loc = sarif_location(primary.rel_file, primary.line, primary.column)
            if loc:
                locations.append(loc)

        results.append({
            "ruleId": rule_id,
            "level": "error",
            "message": {"text": report.error_summary},
            "locations": locations,
        })

    return build_sarif_doc(_TOOL_NAME, _TOOL_URI, list(rules.values()), results)


# -- Markdown summary ----------------------------------------------------------

_TSAN_NOTE = (
    "> **Note:** Some findings may be false positives due to Argobots ULT "
    "context switches. Add suppressions to `utils/test_tsan.supp` as needed."
)

_TSAN_NO_ISSUES = "#### PASS ThreadSanitizer -- No thread-safety issues detected"


def _tsan_row(idx, report):
    """Return summary table cells for one TSan report."""
    primary = _primary_frame(report)
    loc = f"`{primary.rel_file}:{primary.line}`" if primary else "_(no source)_"
    test_name = report.test_name if report.test_name else "_(unknown)_"
    return [str(idx), test_name, f"`{report.error_type}`", loc, report.error_summary[:80]]


def build_summary(reports):
    """Return a Markdown summary block for the GitHub job summary."""
    return build_summary_md(
        tool_name="ThreadSanitizer",
        emoji="FAIL",
        items=reports,
        headers=["#", "Test", "Error type", "Primary location", "Description"],
        row_fn=_tsan_row,
        details_fn=report_heading,
        note=_TSAN_NOTE,
        no_items_msg=_TSAN_NO_ISSUES,
    )


# -- Main ----------------------------------------------------------------------

def main():
    """Entry point."""
    return main_runner(
        get_args("Parse TSan log files and emit SARIF and a summary."),
        collect_fn=_collect_reports,
        resolve_fn=resolve_paths_threaded,
        build_sarif_fn=build_sarif,
        build_summary_fn=build_summary,
        tool_label="TSan",
    )


if __name__ == "__main__":
    sys.exit(main())
