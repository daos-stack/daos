#!/bin/bash
# Swap a gcov-instrumented shared library into the install prefix, run a command, restore the
# library -- so that a test suite loading the installed library (bio_ut, ddb_tests, ...) runs the
# instrumented copy without rebuilding or reinstalling anything.
#
# Usage: cov-swap_run.sh [--env FILE] [--ref PATH] LIBNAME SIDE_DIR -- COMMAND [ARGS...]
#   LIBNAME   file name of the installed library, looked up under $PREFIX/lib64/daos_srv/ then
#             $PREFIX/lib64/ (libbio.so, libddb.so, libvos.so, libdaos_common_pmem.so, ...)
#   SIDE_DIR  directory holding the instrumented LIBNAME (and its .gcno/.gcda files), as produced
#             by the script generated with cov-gen_cmds.py
#   --env FILE  ticket env.sh to source for DAOS_INSTALL/DAOS_BUILD (default: the env.sh next to
#             this script, i.e. the ticket directory it is symlinked into); PREFIX/BUILD in the
#             environment override both
#   --ref PATH  build-dir copy of the library to use as the reference (default: the single file
#             named LIBNAME found under $BUILD, e.g. $BUILD/daos/debug/gcc/src/bio/libbio.so)
# The build-dir copy of the library is the reference: the installed copy must match it before the
# swap (refuse otherwise: a previous swap was not restored, or the install is stale) and is
# restored from it afterwards (checked with cmp), whatever the command's exit status, which is
# returned. Run it on the build node; COMMAND typically is the suite's own runner
# (e.g. ./utils/run_utest.py ... or ddb_tests ...).
set -u

CWD="$(realpath "$(dirname "$0")")"
ENV_FILE="$CWD/env.sh"
REF=""
while [[ $# -gt 0 ]]; do
	case "$1" in
		--env) ENV_FILE="$2"; shift 2 ;;
		--ref) REF="$2"; shift 2 ;;
		-h|--help)
			sed -n '2,/^set -u/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
			exit 0 ;;
		*) break ;;
	esac
done
if [[ $# -lt 3 ]]; then
	echo "Usage: cov-swap_run.sh [--env FILE] [--ref PATH] LIBNAME SIDE_DIR -- COMMAND [ARGS...]" >&2
	exit 2
fi
lib=$1
side=$2
shift 2
[[ "$1" == "--" ]] && shift

if [[ -z "${PREFIX:-}" || -z "${BUILD:-}" ]]; then
	if [[ -r "$ENV_FILE" ]]; then
		# shellcheck disable=SC1090
		source "$ENV_FILE"
	else
		echo "[cov] no env file $ENV_FILE: set PREFIX and BUILD or pass --env FILE" >&2
		exit 2
	fi
fi
PREFIX=${PREFIX:-$DAOS_INSTALL}
BUILD=${BUILD:-$DAOS_BUILD}

# Build-dir copy: scons builds each library in its own src/ subdirectory of the variant dir
# ($BUILD/daos/<build type>/<compiler>/src/bio/libbio.so, .../src/utils/ddb/libddb.so, ...), so
# the file name alone identifies it; the search excludes the install prefix if it is nested.
if [[ -z "$REF" ]]; then
	mapfile -t refs < <(find "$BUILD" -path '*/src/*' -type f -name "$lib" 2>/dev/null |
		grep -vF "$PREFIX/")
	if [[ ${#refs[@]} -ne 1 ]]; then
		echo "[cov] ${#refs[@]} build-dir copies of $lib under $BUILD, pass --ref PATH" >&2
		printf '  %s\n' "${refs[@]}" >&2
		exit 2
	fi
	REF=${refs[0]}
fi
# Installed copy: engine modules live in lib64/daos_srv/, common libraries in lib64/.
inst=$PREFIX/lib64/daos_srv/$lib
[[ -f "$inst" ]] || inst=$PREFIX/lib64/$lib
for f in "$REF" "$inst" "$side/$lib"; do
	[[ -f "$f" ]] || { echo "[cov] missing $f" >&2; exit 2; }
done
if ! cmp -s "$REF" "$inst"; then
	echo "[cov] installed $inst differs from the build copy $REF, refusing to swap" >&2
	exit 2
fi
echo "[cov] swapping $side/$lib into $inst"
\cp -f "$side/$lib" "$inst" || exit 2
"$@"
rc=$?
echo "[cov] command exit status $rc"
\cp -f "$REF" "$inst"
if cmp -s "$REF" "$inst"; then
	echo "[cov] restored $inst"
else
	echo "[cov] FAILED to restore $inst" >&2
	exit 2
fi
ls -la "$side"/*.gcda 2>/dev/null || echo "[cov] no .gcda produced" >&2
exit $rc
