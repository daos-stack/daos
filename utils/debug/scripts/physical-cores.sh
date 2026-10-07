#!/bin/bash
# Prints the number of PHYSICAL CPU cores (not logical/hyperthreaded ones,
# unlike `nproc`) on the local host, for use as a scons/make -j default that
# doesn't over-subscribe a shared build node's real execution units.
#
# Usage:
#   physical-cores.sh
#
# Uses `lscpu -p=CORE,SOCKET` (unique Core+Socket pairs -- correct across
# multiple sockets); falls back to `nproc` (logical count) if `lscpu` is
# unavailable or returns nothing usable. Never fails -- always prints a
# positive integer.
#
# Meant to be run on whichever host the job will actually execute on (e.g.
# via ssh to the build node), not on a caller's local/control host -- core
# counts can differ between them.

set -uo pipefail

n=""
if command -v lscpu >/dev/null 2>&1; then
	n="$(lscpu -p=CORE,SOCKET 2>/dev/null | grep -v '^#' | sort -u | wc -l)"
fi

if [[ -z "$n" || "$n" -eq 0 ]]; then
	n="$(nproc 2>/dev/null)"
fi

if [[ -z "$n" || "$n" -eq 0 ]]; then
	n=1
fi

echo "$n"
