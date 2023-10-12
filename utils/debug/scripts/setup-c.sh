#!/bin/bash
# Setup C environment for DAOS development.
# This script is designed to be *sourced*, not executed directly:
#
#   source /path/to/setup-c.sh
#
# Sets C_INCLUDE_PATH, CPATH, and PKG_CONFIG_PATH from the paths
# provided by .build_vars.sh and the DAOS source tree.

DAOS_SRC_DIR="${DAOS_SRC_DIR:-$HOME/work/daos}"

# ── Validate ──────────────────────────────────────────────────────────────────
if [[ ! -d "$DAOS_SRC_DIR" ]]; then
echo "setup-c.sh: [ERROR] DAOS_SRC_DIR not found: $DAOS_SRC_DIR" >&2; return 1
fi
if [[ ! -f "$DAOS_SRC_DIR/.build_vars.sh" ]]; then
echo "setup-c.sh: [ERROR] .build_vars.sh not found." >&2
echo "  Rebuild: cd $DAOS_SRC_DIR && scons --config=force" >&2; return 1
fi
if [[ ! -f "$DAOS_SRC_DIR/compile_commands.json" ]]; then
echo "setup-c.sh: [ERROR] compile_commands.json not found." >&2
echo "  Rebuild: cd $DAOS_SRC_DIR && scons --config=force" >&2; return 1
fi

# shellcheck disable=SC1090
source "$DAOS_SRC_DIR/.build_vars.sh"

if [[ ! -d "${SL_PREFIX:-}" ]]; then
echo "setup-c.sh: [ERROR] SL_PREFIX=${SL_PREFIX:-<unset>} does not exist." >&2
echo "  Rebuild: cd $DAOS_SRC_DIR && scons --config=force" >&2; return 1
fi


# ── Include paths ─────────────────────────────────────────────────────────────
_add_inc() { [[ -d "$1" ]] && C_INCLUDE_PATH="${C_INCLUDE_PATH:+$C_INCLUDE_PATH:}$1" && CPATH="${CPATH:+$CPATH:}$1"; }
C_INCLUDE_PATH=""
CPATH=""
_add_inc "$DAOS_SRC_DIR/src/include"
_add_inc "$SL_PREFIX/include"
_add_inc "$SL_MERCURY_PREFIX/include"
_add_inc "$SL_OFI_PREFIX/include"
_add_inc "$SL_ARGOBOTS_PREFIX/include"
_add_inc "$SL_SPDK_PREFIX/include/daos_srv"
_add_inc "${SL_PROTOBUFC_PREFIX:-}/include"
# Fallback: system protobuf-c-devel package (used when prereq not built yet)
_add_inc "/usr/include"
unset -f _add_inc
export C_INCLUDE_PATH CPATH

# ── PKG_CONFIG_PATH ───────────────────────────────────────────────────────────
_add_pkg() { [[ -d "$1" ]] && PKG_CONFIG_PATH="${PKG_CONFIG_PATH:+$PKG_CONFIG_PATH:}$1"; }
PKG_CONFIG_PATH=""
_add_pkg "$SL_PREFIX/lib/pkgconfig"
_add_pkg "$SL_PREFIX/lib64/pkgconfig"
unset -f _add_pkg
export PKG_CONFIG_PATH

echo "[INFO] setup-c.sh: ready"
echo "  SL_PREFIX       : $SL_PREFIX"
echo "  C_INCLUDE_PATH  : $C_INCLUDE_PATH"
echo "  PKG_CONFIG_PATH : $PKG_CONFIG_PATH"
