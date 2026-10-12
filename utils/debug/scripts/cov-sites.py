#!/usr/bin/env python3
"""List the error-reporting sites of some functions in a `gcov -b -c -f` file with their hit counts.

Usage: cov-sites.py FILE.gcov FUNC [FUNC ...] [--regex REGEX] [--md]
Within each requested function, every line matching REGEX (default: D_ERROR, DL_ERROR, D_WARN,
DL_CDEBUG calls) is printed with its execution count and the message text, gathered from the
matching line and its continuation lines up to the closing of the call. A count of 0 (`#####`)
means the site was never reached by the run.
"""

import re
import sys

DEFAULT_REGEX = r"\b(D_ERROR|DL_ERROR|D_WARN|DL_CDEBUG)\s*\("


def parse(path):
    funcs = {}
    current = None
    rx_func = re.compile(r"^function (\S+) called (\d+)")
    rx_line = re.compile(r"^\s*([-#=0-9*]+):\s*(\d+):(.*)$")
    for raw in open(path, encoding="utf-8", errors="replace"):
        raw = raw.rstrip("\n")
        m = rx_func.match(raw)
        if m:
            current = funcs.setdefault(m.group(1), [])
            continue
        m = rx_line.match(raw)
        if m and current is not None:
            current.append((m.group(1), int(m.group(2)), m.group(3)))
    return funcs


def message(lines, idx):
    """Join the source text from the call line until the parentheses balance."""
    depth = 0
    out = []
    for _, _, text in lines[idx:]:
        out.append(text.strip())
        depth += text.count("(") - text.count(")")
        if depth <= 0 and out:
            break
    text = " ".join(out)
    # keep the format argument only: from the first string literal to the first top-level comma
    start = text.find('"')
    if start < 0:
        return text
    depth = 0
    in_str = False
    end = len(text)
    i = start
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\":
                i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == "(":
            depth += 1
        elif c == ")":
            if depth == 0:
                end = i
                break
            depth -= 1
        elif c == "," and depth == 0:
            end = i
            break
        i += 1
    fmt = text[start:end].replace('"', "")
    return re.sub(r"\s+", " ", fmt).strip()


def usage(rc):
    print(__doc__.strip(), file=sys.stderr if rc else sys.stdout)
    sys.exit(rc)


def main():
    argv = sys.argv[1:]
    if "-h" in argv or "--help" in argv:
        usage(0)
    if len(argv) < 2:
        usage(2)
    md = "--md" in argv
    argv = [a for a in argv if a != "--md"]
    regex = DEFAULT_REGEX
    if "--regex" in argv:
        i = argv.index("--regex")
        regex = argv[i + 1]
        del argv[i:i + 2]
    path, names = argv[0], argv[1:]
    rx = re.compile(regex)
    funcs = parse(path)
    if md:
        print("| Function | Line | Hits | Message |")
        print("|---|---|---|---|")
    for name in names:
        lines = funcs.get(name)
        if lines is None:
            print(f"{name}: not found")
            continue
        for idx, (count, lineno, text) in enumerate(lines):
            if not rx.search(text):
                continue
            if count == "-":
                hits = "-"
            elif count.startswith(("#", "=")):
                hits = 0
            else:
                hits = int(count.rstrip("*"))
            msg = message(lines, idx).replace("\\n", "").replace("|", "\\|")
            if md:
                print(f"| `{name}` | {lineno} | {hits} | `{msg}` |")
            else:
                print(f"{name:22s} :{lineno:<5d} hits={hits!s:5s} {msg}")


if __name__ == "__main__":
    main()
