#!/usr/bin/env python3
"""
  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
  All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

  Parse AddressSanitizer log files produced during CI unit test runs.

  Outputs:
    • GitHub workflow ::error:: / ::notice:: annotations (inline on PR diffs)
    • SARIF 2.1.0 file  → uploaded to the Security / Code scanning tab
    • Markdown summary  → appended to $GITHUB_STEP_SUMMARY

  Invocation (from .github/workflows/memcheck.yml):

    python3 utils/ci/parse_asan_reports.py \
        --report-dir  asan-logs               \
        --source-root "$(pwd)"                \
        --sarif-out   test-results/asan.sarif \
        --summary-out test-results/asan_summary.md

  ASan must be started with:
    ASAN_OPTIONS="log_path=<dir>/asan:exitcode=42:symbolize=1"
  so that each process writes its own asan.<pid> file and the stack
  frames already contain resolved function / file / line information.
"""

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# ── Data structures ───────────────────────────────────────────────────────────


@dataclass
class StackFrame:
    index: int
    function: str
    file: Optional[str]      # absolute path as reported by ASan / symbolizer
    line: Optional[int]
    column: Optional[int]
    rel_file: Optional[str]  # relative to source_root, filled in later


@dataclass
class AsanReport:
    pid: str
    error_type: str          # e.g. "stack-buffer-overflow"
    error_summary: str       # the first ==PID==ERROR: … line
    access_type: str         # "READ" | "WRITE" | "unknown"
    access_size: Optional[int]
    frames: list = field(default_factory=list)  # list[StackFrame]
    raw: str = ""


# ── Parsing ───────────────────────────────────────────────────────────────────

_RE_ERROR_HEADER = re.compile(
    r"==\d+==ERROR: AddressSanitizer: (?P<type>\S+(?:\s+\S+)*?) on address"
)
_RE_ACCESS = re.compile(r"(?P<access>READ|WRITE) of size (?P<size>\d+)")
_RE_FRAME = re.compile(
    r"^\s+#(?P<idx>\d+)\s+0x[0-9a-f]+"
    r"\s+in\s+(?P<func>\S+)"
    r"(?:\s+(?P<file>[^:()\s]+\.(?:c|cc|cpp|cxx|h|hpp))"
    r":(?P<line>\d+)(?::(?P<col>\d+))?)?",
    re.IGNORECASE,
)


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
    reports = []
    for entry in sorted(report_dir.iterdir()):
        if not re.match(r"asan\.", entry.name):
            continue
        if entry.suffix in (".sarif", ".md"):
            continue
        report = parse_report_file(entry)
        if report:
            reports.append(report)
    return reports


# ── Source-path resolution ────────────────────────────────────────────────────

def resolve_paths(reports: list, source_root: Path) -> None:
    """Populate rel_file for every frame whose absolute path we know."""
    for report in reports:
        for frame in report.frames:
            if not frame.file:
                continue
            try:
                frame.rel_file = str(Path(frame.file).relative_to(source_root))
            except ValueError:
                frame.rel_file = frame.file  # outside checkout – keep as-is


# ── GitHub workflow annotations ───────────────────────────────────────────────

def emit_annotations(reports: list) -> None:
    """
    Emit ::error:: on the innermost in-project frame and ::notice:: for
    the rest of the call chain so every relevant line is annotated.
    """
    for report in reports:
        in_project = [f for f in report.frames if f.rel_file and f.line]
        title = f"ASan: {report.error_type}"
        description = (
            f"{report.access_type} of size {report.access_size}"
            f" — {report.error_type}"
        )

        if not in_project:
            print(f"::error title={title}::{description} (no source location resolved)")
            continue

        primary = in_project[0]
        print(
            f"::error "
            f"file={primary.rel_file},"
            f"line={primary.line},"
            f"col={primary.column or 1},"
            f"title={title}::"
            f"{description} in {primary.function}()"
        )
        for frame in in_project[1:]:
            print(
                f"::notice "
                f"file={frame.rel_file},"
                f"line={frame.line},"
                f"col={frame.column or 1},"
                f"title=ASan call-chain (#{frame.index})::"
                f"Called from {frame.function}()"
                f" — ASan pid={report.pid} ({report.error_type})"
            )


# ── SARIF 2.1.0 ───────────────────────────────────────────────────────────────

_SARIF_SCHEMA = (
    "https://raw.githubusercontent.com/oasis-tcs/sarif-spec"
    "/master/Schemata/sarif-schema-2.1.0.json"
)
_TOOL_URI = "https://clang.llvm.org/docs/AddressSanitizer.html"


def _sarif_location(frame: StackFrame) -> dict:
    loc: dict = {
        "physicalLocation": {
            "artifactLocation": {
                "uri": frame.rel_file or frame.file or "unknown",
                "uriBaseId": "%SRCROOT%",
            }
        }
    }
    if frame.line:
        loc["physicalLocation"]["region"] = {
            "startLine": frame.line,
            "startColumn": frame.column or 1,
        }
    if frame.function:
        loc["logicalLocations"] = [{"name": frame.function, "kind": "function"}]
    return loc


def build_sarif(reports: list, source_root: Path) -> dict:
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
            primary = StackFrame(0, "unknown", None, None, None, None)

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
            "locations": [_sarif_location(primary)],
        }
        if code_flows:
            result["codeFlows"] = code_flows
        results.append(result)

    return {
        "$schema": _SARIF_SCHEMA,
        "version": "2.1.0",
        "runs": [{
            "tool": {
                "driver": {
                    "name": "AddressSanitizer",
                    "informationUri": _TOOL_URI,
                    "rules": rules,
                }
            },
            "originalUriBaseIds": {
                "%SRCROOT%": {"uri": source_root.as_uri() + "/"}
            },
            "results": results,
        }],
    }


def write_sarif(reports: list, source_root: Path, out: Path) -> None:
    """Serialize the SARIF document to *out*."""
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(
        json.dumps(build_sarif(reports, source_root), indent=2),
        encoding="utf-8",
    )
    print(f"SARIF written → {out}  ({len(reports)} finding(s))")


# ── Markdown job summary ──────────────────────────────────────────────────────

def write_summary(reports: list, out: Path) -> None:
    """Write a GitHub-flavoured Markdown summary of all ASan findings."""
    lines = []

    if not reports:
        lines.append("### ✅ AddressSanitizer — No memory-safety issues detected\n")
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text("\n".join(lines), encoding="utf-8")
        return

    lines.append(f"### ❌ AddressSanitizer — {len(reports)} issue(s) detected\n")
    lines.append("| # | Error type | Access | Size | Primary location | Function |")
    lines.append("|---|-----------|--------|------|-----------------|----------|")

    for i, r in enumerate(reports, 1):
        in_proj = [f for f in r.frames if f.rel_file and f.line]
        if in_proj:
            p = in_proj[0]
            loc = f"`{p.rel_file}:{p.line}`"
            func = f"`{p.function}()`"
        else:
            loc, func = "_(no source)_", "_(unknown)_"
        lines.append(
            f"| {i} | `{r.error_type}` | {r.access_type}"
            f" | {r.access_size or '?'} B | {loc} | {func} |"
        )

    lines.append("")
    lines.append("<details>")
    lines.append("<summary>Full ASan reports with annotated call chains</summary>\n")

    for i, r in enumerate(reports, 1):
        lines.append(f"#### Report {i} — `{r.error_type}` (pid {r.pid})\n")
        lines.append("```text")
        lines.append(r.raw.strip())
        lines.append("```\n")

        in_proj = [f for f in r.frames if f.rel_file and f.line]
        if in_proj:
            lines.append("**In-project call chain:**\n")
            lines.append("| Frame | File | Line | Function |")
            lines.append("|-------|------|------|----------|")
            for f in in_proj:
                lines.append(
                    f"| #{f.index} | `{f.rel_file}`"
                    f" | {f.line} | `{f.function}()` |"
                )
            lines.append("")

    lines.append("</details>\n")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"ASan summary written → {out}")


# ── CLI ───────────────────────────────────────────────────────────────────────

def main() -> int:
    """Entry point."""
    ap = argparse.ArgumentParser(
        description="Parse ASan logs → GHA annotations + SARIF + summary"
    )
    ap.add_argument("--report-dir", required=True, type=Path)
    ap.add_argument("--source-root", required=True, type=Path)
    ap.add_argument("--sarif-out", required=True, type=Path)
    ap.add_argument("--summary-out", required=True, type=Path)
    args = ap.parse_args()

    source_root = args.source_root.resolve()
    report_dir = args.report_dir.resolve()

    if not report_dir.is_dir():
        print(f"::warning::ASan log directory not found: {report_dir}")
        write_sarif([], source_root, args.sarif_out)
        write_summary([], args.summary_out)
        return 0

    reports = collect_reports(report_dir)
    resolve_paths(reports, source_root)
    print(f"Parsed {len(reports)} ASan report(s) from {report_dir}")

    emit_annotations(reports)
    write_sarif(reports, source_root, args.sarif_out)
    write_summary(reports, args.summary_out)
    return 1 if reports else 0


if __name__ == "__main__":
    sys.exit(main())
