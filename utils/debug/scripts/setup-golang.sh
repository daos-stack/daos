#!/bin/bash
# Setup Go/CGo environment for DAOS development.
# This script is designed to be *sourced*, not executed directly:
#
#   source /path/to/setup-golang.sh
#
# Sets CGO_CFLAGS, CGO_LDFLAGS, and LD_LIBRARY_PATH from the paths
# provided by .build_vars.sh. Sources .build_vars.sh directly —
# does NOT rely on setup_local.sh or path-walking heuristics.

DAOS_SRC_DIR="${DAOS_SRC_DIR:-$HOME/work/daos}"

# ── Validate ──────────────────────────────────────────────────────────────────
if [[ ! -d "$DAOS_SRC_DIR" ]]; then
echo "setup-golang.sh: [ERROR] DAOS_SRC_DIR not found: $DAOS_SRC_DIR" >&2; return 1
fi
if [[ ! -f "$DAOS_SRC_DIR/.build_vars.sh" ]]; then
echo "setup-golang.sh: [ERROR] .build_vars.sh not found." >&2
echo "  Rebuild: cd $DAOS_SRC_DIR && scons --config=force" >&2; return 1
fi
if [[ ! -f "$DAOS_SRC_DIR/compile_commands.json" ]]; then
echo "setup-golang.sh: [ERROR] compile_commands.json not found." >&2
echo "  Rebuild: cd $DAOS_SRC_DIR && scons --config=force" >&2; return 1
fi

# shellcheck disable=SC1090
source "$DAOS_SRC_DIR/.build_vars.sh"

if [[ ! -d "${SL_PREFIX:-}" ]]; then
echo "setup-golang.sh: [ERROR] SL_PREFIX=${SL_PREFIX:-<unset>} does not exist." >&2
echo "  Rebuild: cd $DAOS_SRC_DIR && scons --config=force" >&2; return 1
fi


# ── CGo flags (directly from SL_* vars provided by .build_vars.sh) ───────────
_add_cc() { [[ -d "$1" ]] && CGO_CFLAGS+=" -I$1"; }
_add_ld() { [[ -d "$1" ]] && CGO_LDFLAGS+=" -L$1"; }
_add_rt() { [[ -d "$1" ]] && LD_LIBRARY_PATH="${LD_LIBRARY_PATH:+$LD_LIBRARY_PATH:}$1"; }

CGO_CFLAGS=""
_add_cc "$DAOS_SRC_DIR/src/include"
_add_cc "$SL_PREFIX/include"
_add_cc "$SL_MERCURY_PREFIX/include"
_add_cc "$SL_OFI_PREFIX/include"
_add_cc "$SL_ARGOBOTS_PREFIX/include"
_add_cc "$SL_SPDK_PREFIX/include/daos_srv"
_add_cc "${SL_PROTOBUFC_PREFIX:-}/include"
CGO_CFLAGS="${CGO_CFLAGS# }"

CGO_LDFLAGS=""
_add_ld "$SL_PREFIX/lib"
_add_ld "$SL_PREFIX/lib64"
_add_ld "$SL_PREFIX/lib64/daos_srv"
_add_ld "$SL_BUILD_DIR/src/control/lib/spdk"
_add_ld "$SL_MERCURY_PREFIX/lib"
_add_ld "$SL_SPDK_PREFIX/lib64/daos_srv"
_add_ld "$SL_OFI_PREFIX/lib"
CGO_LDFLAGS="${CGO_LDFLAGS# }"

LD_LIBRARY_PATH=""
_add_rt "$SL_PREFIX/lib"
_add_rt "$SL_PREFIX/lib64"
_add_rt "$SL_PREFIX/lib64/daos_srv"
_add_rt "$SL_MERCURY_PREFIX/lib"
_add_rt "$SL_SPDK_PREFIX/lib64/daos_srv"
_add_rt "$SL_OFI_PREFIX/lib"

unset -f _add_cc _add_ld _add_rt
export CGO_CFLAGS CGO_LDFLAGS LD_LIBRARY_PATH

# ── Go toolchain settings ─────────────────────────────────────────────────────
export DAOS_BASE="${DAOS_BASE:-${SL_SRC_DIR}}"
export GOTOOLCHAIN="${GOTOOLCHAIN:-auto}"
export GOSUMDB="${GOSUMDB:-sum.golang.org}"
export GOPROXY="${GOPROXY:-https://proxy.golang.org,direct}"

echo "[INFO] setup-golang.sh: ready"
echo "  SL_PREFIX       : $SL_PREFIX"
echo "  LD_LIBRARY_PATH : $LD_LIBRARY_PATH"
echo "  CGO_CFLAGS      : $CGO_CFLAGS"
echo "  CGO_LDFLAGS     : $CGO_LDFLAGS"
