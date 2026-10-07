#!/bin/bash
# Runs a scons/make build command and, if it fails with the specific
# "stale libddb.so loaded via RPATH during ddb's man-page generation" go-flags
# panic described below, syncs the just-rebuilt *.so files from the build
# directory into the install prefix and retries the command exactly once.
#
# Root cause: DAOS's top-level SConstruct does Default(build_prefix) and a
# *separate* env.Alias('install', '$PREFIX') -- so a plain `scons` build (no
# explicit "install" target) never refreshes $PREFIX's shared libraries. But
# src/control/SConscript's ddb man-page Command() (in install_go_bin()) runs
# the just-built `ddb` Go binary directly from the build area as part of
# that same default "build" step, and that binary's RPATH is an absolute,
# hardcoded (DT_RPATH, not DT_RUNPATH) path into $PREFIX/lib64 -- it is not
# overridable by the LD_LIBRARY_PATH that d_enable_ld_path() sets specifically
# to point at the build directory's fresher copy (its own code comment says
# that is the intent). So after switching branches / amending commits that
# touch ddb's C internals -- while reusing one ticket-scoped PREFIX across
# incremental rebuilds -- the freshly-built Go binary loads the STALE
# libddb.so still sitting in $PREFIX from the previous "scons install"
# cycle, and panics:
#   panic: runtime error: invalid memory address or nil pointer dereference
#   ... github.com/jessevdk/go-flags.(*Option).showInHelp ...
#
# This is a genuine upstream SCons dependency-ordering gap (the man-page
# Command only depends on the Go binary, never on the shared libs' own
# Install() nodes) -- a candidate for a real daos-stack/daos fix (e.g. an
# explicit Depends(), or building Go binaries with --enable-new-dtags so
# LD_LIBRARY_PATH can do what d_enable_ld_path()'s comment already says is
# the intent). Not fixed upstream here; this script is the pragmatic local
# workaround, generalized to any similarly-affected lib (not just libddb.so).
#
# Usage:
#   scons-retry-lib-sync.sh <BUILD_DIR> <PREFIX> -- <command...>
#
# <BUILD_DIR>/<PREFIX> are only used to scope the lib-sync (never written to
# otherwise); <command...> is run exactly as given, and re-run unmodified on
# a detected-and-matched retry. Exit status is the command's own (from the
# retry, if one happened).

set -u -o pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
	sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
	exit 0
fi

BUILD_DIR="${1:?BUILD_DIR required -- see --help}"
PREFIX="${2:?PREFIX required -- see --help}"
shift 2
[[ "${1:-}" == "--" ]] && shift
if [[ $# -eq 0 ]]; then
	echo "scons-retry-lib-sync.sh: [ERROR] no command given -- see --help" >&2
	exit 1
fi

# Refreshes any $PREFIX/{lib,lib64,lib64/daos_srv}/*.so that differs from
# the freshest same-named *.so anywhere under $BUILD_DIR. Prints the number
# of files actually refreshed (on stdout); logs each one (on stderr).
_sync_libs() {
	local synced=0 base candidate libdir
	while IFS= read -r base; do
		[[ -z "$base" ]] && continue
		candidate="$(find "$BUILD_DIR" -type f -name "$base" -printf '%T@ %p\n' 2>/dev/null \
			| sort -rn | head -1 | cut -d' ' -f2-)"
		[[ -n "$candidate" ]] || continue
		for libdir in "$PREFIX/lib" "$PREFIX/lib64" "$PREFIX/lib64/daos_srv"; do
			[[ -f "$libdir/$base" ]] || continue
			cmp -s "$candidate" "$libdir/$base" && continue
			echo "[INFO] scons-retry-lib-sync.sh: refreshing stale $libdir/$base from $candidate" >&2
			cp -f "$candidate" "$libdir/$base"
			synced=$((synced + 1))
		done
	done < <(find "$BUILD_DIR" -type f -name '*.so' -printf '%f\n' 2>/dev/null | sort -u)
	echo "$synced"
}

_run_once() {
	local logfile="$1"
	shift
	"$@" 2>&1 | tee "$logfile"
	return "${PIPESTATUS[0]}"
}

LOGFILE="$(mktemp)"
trap 'rm -f "$LOGFILE"' EXIT

_run_once "$LOGFILE" "$@"
STATUS=$?
if [[ "$STATUS" -eq 0 ]]; then
	exit 0
fi

if grep -qF "jessevdk/go-flags" "$LOGFILE" && \
   grep -qF "invalid memory address or nil pointer dereference" "$LOGFILE"; then
	echo "[INFO] scons-retry-lib-sync.sh: detected the known stale-libddb.so go-flags panic" \
	     "(see this script's header comment for the root cause) --" \
	     "syncing build-dir libs into $PREFIX and retrying once" >&2
	synced="$(_sync_libs)"
	if [[ "$synced" -eq 0 ]]; then
		echo "[WARN] scons-retry-lib-sync.sh: panic signature matched but no stale lib needed" \
		     "syncing -- retrying anyway in case of a different stale artifact" >&2
	fi
	_run_once "$LOGFILE" "$@"
	STATUS=$?
	if [[ "$STATUS" -eq 0 ]]; then
		echo "[INFO] scons-retry-lib-sync.sh: retry succeeded" >&2
		exit 0
	fi
	echo "[ERROR] scons-retry-lib-sync.sh: retry also failed -- surfacing the failure" >&2
	exit "$STATUS"
fi

exit "$STATUS"
