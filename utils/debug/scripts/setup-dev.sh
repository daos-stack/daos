#!/bin/bash
# Setup the full DAOS development environment.
# This script is designed to be *sourced*, not executed directly:
#
#   source /path/to/setup-dev.sh
#
# Sources the three language-specific setup scripts in order:
#   1. setup-c.sh      — C_INCLUDE_PATH, CPATH, PKG_CONFIG_PATH
#   2. setup-python.sh — PYTHONPATH
#   3. setup-golang.sh — CGO_CFLAGS, CGO_LDFLAGS, LD_LIBRARY_PATH
#
# Each script is independently sourceable for language-specific setups.
# Override DAOS_SRC_DIR before sourcing to use a different checkout:
#
#   DAOS_SRC_DIR=~/work/daos-alt source setup-dev.sh

export DAOS_SRC_DIR="${DAOS_SRC_DIR:-$HOME/work/daos}"
_SETUP_DEV_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"

_daos_source() {
local script="$_SETUP_DEV_DIR/$1"
if [[ ! -f "$script" ]]; then
echo "setup-dev.sh: [ERROR] $1 not found at $script" >&2
return 1
fi
echo ""
echo "━━━ $1 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
# shellcheck disable=SC1090
source "$script"
}

_daos_source setup-c.sh      || { unset _SETUP_DEV_DIR; unset -f _daos_source; return 1; }
_daos_source setup-python.sh || { unset _SETUP_DEV_DIR; unset -f _daos_source; return 1; }
_daos_source setup-golang.sh || { unset _SETUP_DEV_DIR; unset -f _daos_source; return 1; }

# ── Add DAOS tools to PATH ────────────────────────────────────────────────────
if [[ -d "${SL_PREFIX:-}/bin" ]]; then
case ":$PATH:" in
*":$SL_PREFIX/bin:"*) ;;
*) export PATH="$SL_PREFIX/bin:$PATH" ;;
esac
fi

echo ""
echo "━━━ DAOS development environment ready ━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  DAOS_SRC_DIR : $DAOS_SRC_DIR"
echo "  SL_PREFIX    : ${SL_PREFIX:-<unset>}"
echo "  PATH         : $PATH"
echo ""

unset _SETUP_DEV_DIR
unset -f _daos_source
