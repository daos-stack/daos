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
#
# When not overridden, DAOS_SRC_DIR defaults to this script's own directory
# (not a fixed path), so the same script works unmodified from any checkout
# -- the main ~/work/daos repo or a per-ticket `git worktree` checkout.

_SETUP_DEV_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
export DAOS_SRC_DIR="${DAOS_SRC_DIR:-$_SETUP_DEV_DIR}"

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

# Attempt all three independently -- one failing (e.g. no build yet) must
# not prevent the others from being sourced; each is still independently
# sourceable/useful on its own (see header comment).
_SETUP_DEV_FAILED=""
_daos_source setup-c.sh      || _SETUP_DEV_FAILED="${_SETUP_DEV_FAILED:+$_SETUP_DEV_FAILED, }setup-c.sh"
_daos_source setup-python.sh || _SETUP_DEV_FAILED="${_SETUP_DEV_FAILED:+$_SETUP_DEV_FAILED, }setup-python.sh"
_daos_source setup-golang.sh || _SETUP_DEV_FAILED="${_SETUP_DEV_FAILED:+$_SETUP_DEV_FAILED, }setup-golang.sh"

# ── Add DAOS tools to PATH ────────────────────────────────────────────────────
if [[ -d "${SL_PREFIX:-}/bin" ]]; then
case ":$PATH:" in
*":$SL_PREFIX/bin:"*) ;;
*) export PATH="$SL_PREFIX/bin:$PATH" ;;
esac
fi

echo ""
if [[ -z "$_SETUP_DEV_FAILED" ]]; then
echo "━━━ DAOS development environment ready ━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
else
echo "━━━ DAOS development environment partially ready ━━━━━━━━━━━━━━━━━━"
echo "  Needs a build: $_SETUP_DEV_FAILED (see the hints printed above)"
fi
echo "  DAOS_SRC_DIR : $DAOS_SRC_DIR"
echo "  SL_PREFIX    : ${SL_PREFIX:-<unset>}"
echo "  PATH         : $PATH"
echo ""

unset _SETUP_DEV_DIR
unset -f _daos_source
if [[ -n "$_SETUP_DEV_FAILED" ]]; then
unset _SETUP_DEV_FAILED
return 1
fi
unset _SETUP_DEV_FAILED
