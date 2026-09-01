#!/bin/bash
# Computes the colon-separated scons ALT_PREFIX list of already-built prereq
# install directories (ofi, ucx, mercury, pmdk, spdk, argobots, isal,
# isal_crypto, protobufc, fused, ...) from a shared DAOS install's
# .build_vars.sh, so a ticket-specific build can reuse them instead of
# rebuilding from scratch (see docs/dev/development.md in the DAOS source
# tree for background on ALT_PREFIX).
#
# Usage:
#   compute-daos-alt-prefix.sh [/path/to/shared/install/.build_vars.sh]
#
# Prints the computed ALT_PREFIX value to stdout (empty if the shared
# .build_vars.sh is missing/unreadable, or defines no prereq components --
# never fails, so callers can safely use it unconditionally:
#   daos_alt_prefix="$(compute-daos-alt-prefix.sh)"
#
# Env overrides:
#   DAOS_SHARED_BUILD_VARS  Path to the shared .build_vars.sh
#                           (default: /scratch/$USER/daos-install/install/lib/daos/.build_vars.sh)

set -uo pipefail

BUILD_VARS="${1:-${DAOS_SHARED_BUILD_VARS:-/scratch/$USER/daos-install/install/lib/daos/.build_vars.sh}}"

if [[ ! -f "$BUILD_VARS" ]]; then
echo "compute-daos-alt-prefix.sh: [ERROR] not found: $BUILD_VARS -- printing empty ALT_PREFIX" >&2
exit 0
fi

# Run in a clean subshell so sourcing .build_vars.sh cannot leak SL_* variables
# (or anything else it defines) into the caller's environment.
(
set -uo pipefail
# shellcheck disable=SC1090
source "$BUILD_VARS"

alt_prefix=""
for comp in ${SL_COMPONENTS:-}; do
[[ "$comp" == "PREFIX" ]] && continue
var="SL_$comp"
val="${!var:-}"
[[ -z "$val" ]] && continue
alt_prefix="${alt_prefix:+$alt_prefix:}$val"
done

echo "$alt_prefix"
)
