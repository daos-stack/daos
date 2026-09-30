#!/bin/bash
# Guards against a real, sneaky build-caching bug: scons's build-signature
# database (.sconsign.dblite, under an NFS-shared $HOME) is keyed by the
# absolute $DAOS_BUILD path string -- two sandboxes building from the same
# $DAOS_SRC tree with the same (non-isolated) $DAOS_BUILD string on the same
# build node can silently skip recompiling a changed file, because the
# signature looks identical even though the underlying content differs. This
# means a build script can report "scons: done building targets." while the
# resulting object/library files are actually still the PREVIOUS build's
# stale binaries, and any tests run against them validate the wrong code
# without any visible error.
#
# Compares the mtime of one or more source files against their built object
# files (and optionally a final linked library), all read remotely on
# $BUILD_NODE via ssh. Prints "[STALE] ..." and exits non-zero if any
# object/library is NOT newer than its source (i.e. the last rebuild did not
# actually recompile it); prints "[OK] ..." for each fresh file otherwise.
#
# Requires BUILD_NODE, DAOS_SRC, DAOS_BUILD exported in the environment
# (e.g. by a ticket's env.sh). Meant to be called from a thin per-feature
# wrapper that bakes in its own feature's source/artifact list (see
# DAOS-17321's check-ddb_build_freshness.sh for an example), not invoked
# directly with no arguments.
#
# Usage:
#   check-build_freshness.sh [--build-root DIR] [--lib PATH]
#                             <src-rel>:<obj-rel>[,obj-rel...] [...]
#
# <src-rel> and <obj-rel> are paths relative to $DAOS_SRC and --build-root
# (default: $DAOS_BUILD/daos/debug/gcc) respectively. --lib PATH (also
# relative to --build-root) is checked against ALL the given source files:
# it must be newer than every one of them, since any of them changing should
# trigger a relink even if a compile step were (incorrectly) skipped.

set -u -o pipefail

: "${BUILD_NODE:?BUILD_NODE must be exported (e.g. by sourcing env.sh)}"
: "${DAOS_SRC:?DAOS_SRC must be exported (e.g. by sourcing env.sh)}"
: "${DAOS_BUILD:?DAOS_BUILD must be exported (e.g. by sourcing env.sh)}"

BUILD_ROOT="$DAOS_BUILD/daos/debug/gcc"
LIB_REL=""
CHECKS=()
while [[ $# -gt 0 ]]; do
	case "$1" in
		--build-root) BUILD_ROOT="$2"; shift 2 ;;
		--lib) LIB_REL="$2"; shift 2 ;;
		*) CHECKS+=("$1"); shift ;;
	esac
done

if [[ "${#CHECKS[@]}" -eq 0 ]]; then
	echo "[ERROR] no <src-rel>:<obj-rel>[,obj-rel...] pairs given -- see this script's header comment" >&2
	exit 1
fi

STALE=0

for entry in "${CHECKS[@]}" ; do
	SRC_REL="${entry%%:*}"
	OBJ_RELS="${entry#*:}"
	SRC_PATH="$DAOS_SRC/$SRC_REL"

	SRC_MTIME=$(ssh "$BUILD_NODE" stat -c %Y "$SRC_PATH" 2>/dev/null)
	if [[ -z "$SRC_MTIME" ]] ; then
		echo "[ERROR] Could not stat source file $SRC_PATH on $BUILD_NODE" >&2
		STALE=1
		continue
	fi

	IFS=',' read -ra OBJ_REL_ARR <<< "$OBJ_RELS"
	for obj_rel in "${OBJ_REL_ARR[@]}" ; do
		OBJ_PATH="$BUILD_ROOT/$obj_rel"
		OBJ_MTIME=$(ssh "$BUILD_NODE" stat -c %Y "$OBJ_PATH" 2>/dev/null)
		if [[ -z "$OBJ_MTIME" ]] ; then
			echo "[STALE] $obj_rel does not exist on $BUILD_NODE (never built) for $SRC_REL"
			STALE=1
		elif [[ "$OBJ_MTIME" -lt "$SRC_MTIME" ]] ; then
			echo "[STALE] $obj_rel is OLDER than $SRC_REL on $BUILD_NODE" \
			     "-- the last rebuild did NOT recompile this file!"
			STALE=1
		else
			echo "[OK] $obj_rel is newer than $SRC_REL on $BUILD_NODE"
		fi
	done
done

# The final linked library (if given) must be newer than EVERY source file,
# since any of them changing should trigger a relink even if a compile step
# were (incorrectly) skipped.
if [[ -n "$LIB_REL" ]] ; then
	LIB_PATH="$BUILD_ROOT/$LIB_REL"
	LIB_MTIME=$(ssh "$BUILD_NODE" stat -c %Y "$LIB_PATH" 2>/dev/null)
	if [[ -z "$LIB_MTIME" ]] ; then
		echo "[STALE] $LIB_REL does not exist on $BUILD_NODE (never built)"
		STALE=1
	else
		LIB_STALE=0
		for entry in "${CHECKS[@]}" ; do
			SRC_REL="${entry%%:*}"
			SRC_PATH="$DAOS_SRC/$SRC_REL"
			SRC_MTIME=$(ssh "$BUILD_NODE" stat -c %Y "$SRC_PATH" 2>/dev/null)
			if [[ -n "$SRC_MTIME" && "$LIB_MTIME" -lt "$SRC_MTIME" ]] ; then
				echo "[STALE] $LIB_REL is OLDER than $SRC_REL on $BUILD_NODE" \
				     "-- it was not relinked against the latest object files!"
				STALE=1
				LIB_STALE=1
			fi
		done
		if [[ "$LIB_STALE" -eq 0 ]] ; then
			echo "[OK] $LIB_REL is newer than all given source files on $BUILD_NODE"
		fi
	fi
fi

echo
if [[ "$STALE" -ne 0 ]] ; then
	echo "[ERROR] Build on $BUILD_NODE is STALE for one or more files above." \
	     "Force-delete the flagged object/library files (and installed" \
	     "binaries under \$DAOS_INSTALL if applicable) on $BUILD_NODE, then" \
	     "rebuild before trusting test results."
	exit 1
fi
echo "[OK] All checked build artifacts on $BUILD_NODE are newer than their sources."
