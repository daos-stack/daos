#!/usr/bin/env python3
"""Summarize a `gcov -b -c -f` annotated file per function.

Usage: cov-report.py FILE.gcov[,FILE2.gcov...] FUNC [FUNC ...] [--md]
Several comma-separated .gcov files of the same source (e.g. the shared-library and the static
test-binary objects) are merged: calls are summed, a line or branch counts as executed/taken when
it is in any of the files.
Each function block starts at the `function NAME called ...` marker emitted with -f and ends at
the next marker. For each requested function the script prints the executable line count, the
executed line count, the branch count, the branches taken at least once, the never-executed
lines (`#####`) with their source text, and the branches never taken.
"""

import re
import sys


def parse(path):
    funcs = {}
    current = None
    rx_func = re.compile(r"^function (\S+) called (\d+) returned (\d+)% blocks executed (\d+)%")
    rx_line = re.compile(r"^\s*([-#=0-9*]+):\s*(\d+):(.*)$")
    rx_branch = re.compile(r"^branch\s+(\d+)\s+(taken (\d+)|never executed)")
    last_line = None
    for raw in open(path, encoding="utf-8", errors="replace"):
        raw = raw.rstrip("\n")
        m = rx_func.match(raw)
        if m:
            current = funcs.setdefault(
                m.group(1), {"called": int(m.group(2)), "lines": [], "branches": []})
            continue
        m = rx_line.match(raw)
        if m:
            count, lineno, text = m.group(1), int(m.group(2)), m.group(3)
            last_line = (lineno, text)
            if current is not None and count != "-" and lineno > 0:
                executed = not count.startswith("#") and not count.startswith("=")
                current["lines"].append((lineno, executed, text.strip()))
            continue
        m = rx_branch.match(raw)
        if m and current is not None and last_line is not None:
            taken = m.group(3)
            current["branches"].append((last_line[0], int(m.group(1)),
                                        taken is not None and int(taken) > 0))
    return funcs


def merge(all_funcs):
    merged = {}
    for funcs in all_funcs:
        for name, f in funcs.items():
            m = merged.get(name)
            if m is None:
                merged[name] = {"called": f["called"], "lines": list(f["lines"]),
                                "branches": list(f["branches"])}
                continue
            m["called"] += f["called"]
            by_line = {ln: i for i, (ln, _, _) in enumerate(m["lines"])}
            for ln, e, t in f["lines"]:
                i = by_line.get(ln)
                if i is None:
                    m["lines"].append((ln, e, t))
                elif e and not m["lines"][i][1]:
                    m["lines"][i] = (ln, True, t)
            by_br = {(ln, b): i for i, (ln, b, _) in enumerate(m["branches"])}
            for ln, b, t in f["branches"]:
                i = by_br.get((ln, b))
                if i is None:
                    m["branches"].append((ln, b, t))
                elif t and not m["branches"][i][2]:
                    m["branches"][i] = (ln, b, True)
            m["lines"].sort()
    return merged


def usage(rc):
    print(__doc__.strip(), file=sys.stderr if rc else sys.stdout)
    sys.exit(rc)


def main():
    if "-h" in sys.argv or "--help" in sys.argv:
        usage(0)
    if len(sys.argv) < 3:
        usage(2)
    args = [a for a in sys.argv[1:] if a != "--md"]
    md = "--md" in sys.argv
    path, names = args[0], args[1:]
    funcs = merge([parse(p) for p in path.split(",")])
    rows = []
    details = []
    for name in names:
        f = funcs.get(name)
        if f is None:
            rows.append((name, "-", "-", "-", "-", "-", "not found"))
            continue
        lines = f["lines"]
        n_exec = sum(1 for _, e, _ in lines if e)
        br = f["branches"]
        n_taken = sum(1 for _, _, t in br if t)
        pct = f"{100.0 * n_exec / len(lines):.1f}%" if lines else "-"
        bpct = f"{100.0 * n_taken / len(br):.1f}%" if br else "-"
        rows.append((name, f["called"], f"{n_exec}/{len(lines)}", pct, f"{n_taken}/{len(br)}",
                     bpct, ""))
        missed = [(ln, t) for ln, e, t in lines if not e]
        never = [(ln, b) for ln, b, t in br if not t]
        details.append((name, missed, never))
    if md:
        print("| Function | Calls | Lines executed | % | Branches taken | % |")
        print("|---|---|---|---|---|---|")
        for r in rows:
            print(f"| `{r[0]}` | {r[1]} | {r[2]} | {r[3]} | {r[4]} | {r[5]} |")
    else:
        for r in rows:
            print(f"{r[0]:28s} calls={r[1]!s:6s} lines={r[2]:8s} {r[3]:7s} "
                  f"branches={r[4]:8s} {r[5]} {r[6]}")
    for name, missed, never in details:
        if missed:
            print(f"\n{name}: lines never executed")
            for ln, t in missed:
                print(f"  {ln}: {t}")
        if never:
            print(f"\n{name}: branches never taken (line, branch index)")
            print("  " + ", ".join(f"{ln}/{b}" for ln, b in never))


if __name__ == "__main__":
    main()
