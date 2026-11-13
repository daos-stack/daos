#!/usr/bin/env python3
"""
  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
  All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

  Parse AddressSanitizer log files produced during CI unit test runs.

  Outputs:
    - SARIF 2.1.0 file  -> uploaded to the Security / Code scanning tab
    - Markdown summary  -> appended to $GITHUB_STEP_SUMMARY

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

from sanitizer_report_base import (ADDR_FRAME_RE, StackFrame, build_sarif_doc, build_summary_md,
                                   collect_reports, get_args, main_runner, report_heading,
                                   resolve_paths_frames, sarif_location)

# -- Data structures -----------------------------------------------------------


class AsanReport:
    # pylint: disable=too-many-instance-attributes,too-few-public-methods
    """One ASan error report parsed from a single asan.<pid> log file."""

    def __init__(self, pid, error_type, error_summary, access_type, access_size,
                 frames=None, raw="", test_name=""):
        self.pid = pid
        self.error_type = error_type        # e.g. "stack-buffer-overflow"
        self.error_summary = error_summary  # the first ==PID==ERROR: ... line
        self.access_type = access_type      # "READ" | "WRITE" | "unknown"
        self.access_size = access_size
        self.frames = frames if frames is not None else []  # list[StackFrame]
        self.raw = raw
        self.test_name = test_name          # suite name, filled in by collect_reports()


# -- Parsing -------------------------------------------------------------------

_ERROR_HEADER_RE = re.compile(
    r"==\d+==ERROR: AddressSanitizer: (?P<type>\S+(?:\s+\S+)*?) on address"
)
_ACCESS_RE = re.compile(r"(?P<access>READ|WRITE) of size (?P<size>\d+)")


def parse_report_file(path):
    """Parse one ASan log file. Returns None when it contains no error."""
    text = path.read_text(errors="replace")
    if "AddressSanitizer" not in text:
        return None

    pid = re.sub(r"^.*\.", "", path.name)  # asan.12345 -> "12345"
    error_type = "unknown"
    error_summary = ""
    access_type = "unknown"
    access_size = None
    frames = []

    for line in text.splitlines():
        error_reader_match = _ERROR_HEADER_RE.search(line)
        if error_reader_match:
            error_type = error_reader_match.group("type").strip()
            error_summary = line.strip()
            continue

        access_match = _ACCESS_RE.search(line)
        if access_match and access_type == "unknown":
            access_type = access_match.group("access")
            access_size = int(access_match.group("size"))
            continue

        addr_frame_match = ADDR_FRAME_RE.match(line)
        if addr_frame_match:
            frames.append(StackFrame(
                index=int(addr_frame_match.group("idx")),
                function=addr_frame_match.group("func"),
                file=addr_frame_match.group("file"),
                line=int(addr_frame_match.group("line"))
                if addr_frame_match.group("line") else None,
                column=int(addr_frame_match.group("col"))
                if addr_frame_match.group("col") else None,
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


def _collect_reports(report_dir):
    """Return parsed AsanReport objects for every asan.<pid> file found."""
    return collect_reports(report_dir, "asan", parse_report_file)


# -- SARIF 2.1.0 ---------------------------------------------------------------

_TOOL_URI = "https://clang.llvm.org/docs/AddressSanitizer.html"


def _sarif_location(frame):
    """Build a SARIF location with optional logicalLocations for function name."""
    loc = sarif_location(frame.rel_file, frame.line, frame.column)
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


def build_sarif(reports):
    """Build a SARIF 2.1.0 document from the parsed ASan reports."""
    rule_ids = sorted({report.error_type for report in reports})
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

        if in_project:
            primary = in_project[0]
        elif all_framed:
            primary = all_framed[0]
        else:
            # No usable frame -- skip: SARIF requires a physicalLocation.
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
        result = {
            "ruleId": report.error_type,
            "level": "error",
            "message": {"text": msg},
            "locations": [primary_loc],
        }
        if code_flows:
            result["codeFlows"] = code_flows
        results.append(result)

    return build_sarif_doc("AddressSanitizer", _TOOL_URI, rules, results)


# -- Markdown job summary ------------------------------------------------------

def _asan_row(idx, report):
    """Return summary table cell values for one ASan report."""
    loc, func = "_(no source)_", "_(unknown)_"
    for frame in report.frames:
        if not frame.rel_file or not frame.line:
            continue
        loc = f"`{frame.rel_file}:{frame.line}`"
        func = f"`{frame.function}()`"
        break

    test_name = "_(unknown)_"
    if report.test_name:
        test_name = report.test_name

    return [
        str(idx),
        test_name,
        f"`{report.error_type}`",
        report.access_type,
        f"{report.access_size or '?'} B",
        loc,
        func,
    ]


def build_summary(reports):
    """Return a GitHub-flavoured Markdown summary of all ASan findings."""
    return build_summary_md(
        tool_name="AddressSanitizer",
        emoji="FAIL",
        items=reports,
        headers=["#", "Test", "Error type", "Access", "Size", "Primary location", "Function"],
        row_fn=_asan_row,
        details_fn=report_heading,
        no_items_msg="#### PASS AddressSanitizer -- No memory-safety issues detected",
    )

# -- CLI -----------------------------------------------------------------------


def main():
    """Entry point."""
    return main_runner(
        get_args("Parse ASan logs -> SARIF + summary"),
        collect_fn=_collect_reports,
        resolve_fn=resolve_paths_frames,
        build_sarif_fn=build_sarif,
        build_summary_fn=build_summary,
        tool_label="ASan",
    )


if __name__ == "__main__":
    sys.exit(main())
