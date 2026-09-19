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
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from sanitizer_report_base import FILE_LINE_COL, StackFrame, build_sarif_doc, build_summary_md
from sanitizer_report_base import collect_reports as _collect_reports
from sanitizer_report_base import (get_args, main_runner, report_heading, resolve_paths_threaded,
                                   sarif_location)

# ── Data structures ───────────────────────────────────────────────────────────


@dataclass
class TsanThread:
    """One stack (write/read/mutex thread) within a TSan report.

    Only the frames are needed: the descriptive role line ("Write of size 4
    at 0xADDR by thread T1:", etc.) is already preserved verbatim in the
    report's own `raw` text, which is what gets rendered.
    """
    frames: list = field(default_factory=list)  # list[StackFrame]


@dataclass
class TsanReport:
    """One TSan error report (race, deadlock, etc.) parsed from a tsan.<pid> log."""

    pid: str
    error_type: str      # e.g. "data-race", "lock-order-inversion", "use-of-invalid-mutex"
    error_summary: str   # the full WARNING: ... line
    threads: list = field(default_factory=list)  # list[TsanThread]
    raw: str = ""
    test_name: str = ""  # suite name, filled in by collect_reports()


# ── Parsing ───────────────────────────────────────────────────────────────────

# Matches the header line of a TSan report:
#   WARNING: ThreadSanitizer: data race (pid=12345)
_RE_HEADER = re.compile(
    r"WARNING: ThreadSanitizer:\s+(?P<type>.+?)\s+\(pid=(?P<pid>\d+)\)"
)

# Matches a thread role descriptor line:
#   Write of size 4 at 0xADDR by thread T1:
#   Previous read of size 4 at 0xADDR by main thread:
#   Mutex M1 ... created by main thread:
_RE_THREAD_ROLE = re.compile(
    r"^  (?:(?:Previous|Subsequent)\s+)?(?:Write|Read|Mutex|Lock)\s+",
    re.IGNORECASE,
)

# Matches TSan stack frames:
#   #0 func_name /path/file.c:42 (binary+0xaddr)
#   #0 func_name /path/file.c:42:7 (binary+0xaddr)
# Lacks the "0xADDR" address ASan/UBSan have and has a trailing "(binary+off)",
# so it composes its own regex from the shared FILE_LINE_COL fragment rather
# than reusing ADDR_FRAME_RE.
_RE_FRAME = re.compile(
    rf"^\s+#(?P<idx>\d+)\s+(?P<func>\S+)"
    rf"(?:\s+{FILE_LINE_COL})?"
    rf"(?:\s+\([^)]+\))?$",
    re.IGNORECASE,
)


def _normalize_error_type(raw: str) -> str:
    """Convert 'data race' → 'data-race', etc."""
    return re.sub(r"[^a-z0-9]+", "-", raw.lower().strip()).strip("-") or "race"


def parse_report_file(path: Path) -> list:
    """Parse one TSan log file. Returns a list of TsanReport objects."""
    text = path.read_text(errors="replace")
    if "ThreadSanitizer" not in text:
        return []

    reports = []
    current: Optional[TsanReport] = None
    current_thread: Optional[TsanThread] = None

    for line in text.splitlines():
        m = _RE_HEADER.match(line)
        if m:
            # Commit any in-progress report
            if current_thread is not None and current is not None:
                current.threads.append(current_thread)
                current_thread = None
            if current is not None:
                reports.append(current)
            current = TsanReport(
                pid=m.group("pid"),
                error_type=_normalize_error_type(m.group("type")),
                error_summary=line.strip(),
                threads=[],
                raw=line + "\n",
            )
            continue

        if current is None:
            continue
        current.raw += line + "\n"

        # Detect thread role lines
        if _RE_THREAD_ROLE.match(line):
            if current_thread is not None:
                current.threads.append(current_thread)
            current_thread = TsanThread(frames=[])
            continue

        # Stack frames
        m = _RE_FRAME.match(line)
        if m and current_thread is not None:
            current_thread.frames.append(StackFrame(
                index=int(m.group("idx")),
                function=m.group("func"),
                file=m.group("file"),
                line=int(m.group("line")) if m.group("line") else None,
                column=int(m.group("col")) if m.group("col") else None,
                rel_file=None,
            ))

    # Commit any in-progress report
    if current_thread is not None and current is not None:
        current.threads.append(current_thread)
    if current is not None:
        reports.append(current)

    return reports


def collect_reports(report_dir: Path) -> list:
    """Return parsed TsanReport objects for every tsan.<pid> file found."""
    return _collect_reports(report_dir, "tsan", parse_report_file)


def _primary_frame(report: TsanReport) -> Optional[StackFrame]:
    """Return the innermost in-project frame from the first thread stack."""
    for thread in report.threads:
        for frame in thread.frames:
            if frame.rel_file and frame.line:
                return frame
    return None


# ── SARIF 2.1.0 ───────────────────────────────────────────────────────────────

_TOOL_NAME = "ThreadSanitizer"
_TOOL_URI = "https://clang.llvm.org/docs/ThreadSanitizer.html"


def build_sarif(reports: list) -> dict:
    """Build a SARIF 2.1.0 document from the list of TsanReport objects."""
    rules: dict = {}
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


# ── Markdown summary ──────────────────────────────────────────────────────────

_TSAN_NOTE = (
    "> **Note:** Some findings may be false positives due to Argobots ULT "
    "context switches. Add suppressions to `utils/test_tsan.supp` as needed."
)

_TSAN_NO_ISSUES = "#### ✅ ThreadSanitizer — No thread-safety issues detected"


def _tsan_row(i: int, report) -> list:
    """Return summary table cells for one TSan report."""
    primary = _primary_frame(report)
    loc = f"`{primary.rel_file}:{primary.line}`" if primary else "_(no source)_"
    test_name = report.test_name or "_(unknown)_"
    return [str(i), test_name, f"`{report.error_type}`", loc, report.error_summary[:80]]


def build_summary(reports: list) -> str:
    """Return a Markdown summary block for the GitHub job summary."""
    return build_summary_md(
        tool_name="ThreadSanitizer",
        emoji="❌",
        items=reports,
        headers=["#", "Test", "Error type", "Primary location", "Description"],
        row_fn=_tsan_row,
        details_fn=report_heading,
        note=_TSAN_NOTE,
        no_items_msg=_TSAN_NO_ISSUES,
    )


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> int:
    """Entry point."""
    return main_runner(
        get_args("Parse TSan log files and emit SARIF and a summary."),
        collect_fn=collect_reports,
        resolve_fn=resolve_paths_threaded,
        build_sarif_fn=build_sarif,
        build_summary_fn=build_summary,
        tool_label="TSan",
    )


if __name__ == "__main__":
    sys.exit(main())
