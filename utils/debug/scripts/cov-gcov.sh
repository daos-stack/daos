#!/bin/sh
# Produce per-source `gcov -b -c -f` annotated files for instrumented objects.
#
# Usage: cov-gcov.sh SIDE_DIR GCDA_BASENAME...
#   Run from the DAOS source root (the objects were compiled with source paths relative to it).
#   For each GCDA_BASENAME (e.g. b_bio_wal) the annotated output is written to stdout by gcov and
#   split into SIDE_DIR/<basename>--<path_with_underscores>.gcov files, one per source file, so
#   that nothing is written into the source tree and two objects built from the same source (e.g.
#   the shared-library and the static-test copies) keep separate outputs. Feed the .gcov files to
#   cov-report.py / cov-sites.py. See README-coverage.md.
set -eu
if [ $# -lt 2 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
	sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
	[ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]
	exit $?
fi
side=$1
shift
for base in "$@"; do
	gcda=$side/$base.gcda
	[ -f "$gcda" ] || { echo "[cov] missing $gcda" >&2; exit 2; }
	echo "[cov] gcov $gcda"
	gcov -b -c -f -t "$gcda" 2>&1 | awk -v side="$side" -v base="$base" '
		/^ *-: *0:Source:/ { f = $0; sub(/.*Source:/, "", f); gsub("/", "_", f); out = side "/" base "--" f ".gcov" }
		out { print > out }'
done
ls -la "$side"/*.gcov
