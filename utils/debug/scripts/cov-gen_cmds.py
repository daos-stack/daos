#!/usr/bin/env python3
"""Generate a shell script which rebuilds a few DAOS objects with gcov instrumentation.

The scons build log is scanned for the compile line of each requested object and for the link
line of the consumer (a shared library or a test binary). The compile lines are re-emitted with
`--coverage` and their output redirected into a side directory (so the `.gcno`/`.gcda` files live
there and the scons tree stays untouched); the link line is re-emitted with `--coverage`, the
instrumented objects substituted and its output redirected into the same side directory.

Usage: cov-gen_cmds.py --cov-dir DIR --target NAME --obj PATH [--obj PATH ...] LOG [LOG ...]
  --cov-dir DIR   side directory for the instrumented objects, the consumer and the .gcno/.gcda
                  files (e.g. $COV_DIR/patch-001 with COV_DIR from the ticket's env.sh)
  --target NAME   basename of the link output to find (e.g. libbio.so, libddb.so, ddb_ut)
  --obj PATH      object path as it appears in the build log (e.g. .../src/bio/b_bio_wal.os for
                  a shared-library object, .../src/utils/ddb/ddb_wal.o for a static test object)
  LOG             scons build log(s) of the tree to instrument (build-isolated.sh output); an
                  incremental log lacks the lines of untouched objects, add an older full log
The generated script is printed on stdout; run it with `sh` from the DAOS source root, on the
build node. See README-coverage.md for the whole workflow.
"""

import argparse
import re
import sys


def find_line(logs, pattern):
    rx = re.compile(pattern)
    for log in logs:
        with open(log, encoding="utf-8", errors="replace") as fd:
            for line in fd:
                if rx.search(line):
                    return line.rstrip("\n")
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cov-dir", required=True, metavar="DIR")
    parser.add_argument("--target", required=True, metavar="NAME")
    parser.add_argument("--obj", action="append", required=True, metavar="PATH")
    parser.add_argument("logs", nargs="+", metavar="LOG")
    args = parser.parse_args()

    out = ["set -eu", f"mkdir -p {args.cov_dir}", f"echo '[cov] side dir {args.cov_dir}'"]
    subst = {}
    for obj in args.obj:
        line = find_line(args.logs, rf" -o {re.escape(obj)} -c ")
        if line is None:
            sys.exit(f"compile line not found for {obj}")
        cov_obj = f"{args.cov_dir}/{obj.rsplit('/', 1)[1]}"
        subst[obj] = cov_obj
        line = line.replace(f" -o {obj} -c ", f" -o {cov_obj} --coverage -c ", 1)
        out.append(f"echo '[cov] compiling {cov_obj}'")
        out.append(line)

    link = find_line(args.logs, rf" -o \S+/{re.escape(args.target)} ")
    if link is None:
        sys.exit(f"link line not found for {args.target}")
    m = re.search(rf" -o (\S+/{re.escape(args.target)}) ", link)
    cov_target = f"{args.cov_dir}/{args.target}"
    link = link.replace(f" -o {m.group(1)} ", f" -o {cov_target} --coverage ", 1)
    for obj, cov_obj in subst.items():
        if f" {obj} " not in link + " ":
            sys.exit(f"{obj} is not linked into {args.target}")
        link = re.sub(rf" {re.escape(obj)}(?= |$)", f" {cov_obj}", link, count=1)
    out.append(f"echo '[cov] linking {cov_target}'")
    out.append(link)
    out.append(f"echo '[cov] done: {cov_target}'")
    print("\n".join(out))


if __name__ == "__main__":
    main()
