#!/bin/bash
# Setup Python environment for DAOS development.
# This script is designed to be *sourced*, not executed directly:
#
#   source /path/to/setup-python.sh
#
# Builds PYTHONPATH from all top-level Python packages in the DAOS source
# tree, then adds installed Python extensions from SL_PREFIX.
# Sources .build_vars.sh directly — does NOT rely on setup_local.sh.
#
# DAOS_SRC_DIR defaults to this script's own directory (not a fixed path),
# so the same script works unmodified from any checkout -- the main
# ~/work/daos repo or a per-ticket `git worktree` checkout -- as long as it
# is sourced by relative (./setup-python.sh) or absolute in-tree path.
# Explicit override still works:
#   DAOS_SRC_DIR=~/work/daos-alt source setup-python.sh

DAOS_SRC_DIR="${DAOS_SRC_DIR:-$(realpath "$(dirname "${BASH_SOURCE[0]}")")}"

# Prints how to finish building, tailored to the checkout in use: a
# per-ticket worktree (sibling env.sh + inventory.yml one level up) builds
# remotely via ansible-playbook + build-daos.sh, not local scons.
_daos_build_hint() {
local ticket_dir="$DAOS_SRC_DIR/.."
if [[ -f "$ticket_dir/env.sh" && -f "$ticket_dir/inventory.yml" ]]; then
ticket_dir="$(realpath "$ticket_dir")"
echo "  This is a per-ticket worktree -- finish setting it up:" >&2
echo "    1. Review $ticket_dir/inventory.yml" >&2
echo "    2. From \${DAOS_TOOLS_DIR:-~/work/daos-tools}/utils/ansible/ftest/:" >&2
echo "         ansible-playbook -i $ticket_dir/inventory.yml ftest.yml" >&2
echo "    3. $ticket_dir/build-daos.sh --force --deps" >&2
echo "    4. direnv reload (or cd out and back in)" >&2
else
echo "  Rebuild: cd $DAOS_SRC_DIR && scons --config=force" >&2
fi
}

# ── Validate ──────────────────────────────────────────────────────────────────
if [[ ! -d "$DAOS_SRC_DIR" ]]; then
echo "setup-python.sh: [ERROR] DAOS_SRC_DIR not found: $DAOS_SRC_DIR" >&2; unset -f _daos_build_hint; return 1
fi

# ── Build PYTHONPATH from source tree ────────────────────────────────────────
# Needs no build artifacts at all -- works even before the tree is built.
# Strategy: find every __init__.py, add its grandparent to PYTHONPATH when the
# parent is a top-level package (grandparent has no __init__.py).
# Special case: ftest/ and ftest/util/ are added directly (flat import style).
# Stub/shim directories listed in _STUB_DIRS are excluded: they are pylint-only
# shims that would shadow real packages at runtime if added to PYTHONPATH.
_daos_build_pythonpath() {
local daos_src_dir=$1
local -A seen=()

# Pylint-only stubs and shims that must NOT shadow real packages at runtime.
# Add new entries here if a future shim is introduced.
local -a _STUB_DIRS=(
    "*/fake_scons/*"   # SCons shim: shadows SCons at runtime, causing PermissionError
)

local -a _excl_args=()
for _stub in "${_STUB_DIRS[@]}"; do
    _excl_args+=(! -path "$_stub")
done

while IFS= read -r init_py; do
local pkg_dir parent_dir
pkg_dir=$(dirname "$init_py")
parent_dir=$(dirname "$pkg_dir")
[[ -f "$parent_dir/__init__.py" ]] && continue
[[ -n "${seen[$parent_dir]+x}" ]] && continue
seen[$parent_dir]=1
echo "[INFO] setup-python.sh: adding $(basename "$pkg_dir") → $parent_dir"
PYTHONPATH="${PYTHONPATH:+$PYTHONPATH:}$parent_dir"
done < <(find "$daos_src_dir" -name "__init__.py" \
"${_excl_args[@]}"            \
! -path "*/.git/*"            \
! -path "*/build/*"           \
! -path "*/__pycache__/*"     \
! -path "*/.venv/*"           \
! -path "*/venv/*"            \
! -path "*/ftest/cart/util/*" \
| sort)

local ftest_dir="${daos_src_dir}/src/tests/ftest"
if [[ -d "$ftest_dir" && -z "${seen[$ftest_dir]+x}" ]]; then
echo "[INFO] setup-python.sh: adding ftest → $ftest_dir"
PYTHONPATH="${PYTHONPATH:+$PYTHONPATH:}$ftest_dir"
fi
local ftest_util="${daos_src_dir}/src/tests/ftest/util"
if [[ -d "$ftest_util" && -z "${seen[$ftest_util]+x}" ]]; then
echo "[INFO] setup-python.sh: adding ftest/util → $ftest_util"
PYTHONPATH="${PYTHONPATH:+$PYTHONPATH:}$ftest_util"
fi
export PYTHONPATH
}

echo "[INFO] setup-python.sh: building PYTHONPATH from source tree"
_daos_build_pythonpath "$DAOS_SRC_DIR"
unset -f _daos_build_pythonpath

# ── Install-tree Python paths (compiled extensions from SL_PREFIX) ────────────
# Provides: pydaos_shim.so and other compiled C extensions not in the source
# tree. Needs a prior build -- skipped (not fatal) until one exists, so
# PYTHONPATH from the source tree above still works pre-build.
if [[ -f "$DAOS_SRC_DIR/.build_vars.sh" && -f "$DAOS_SRC_DIR/compile_commands.json" ]]; then
# shellcheck disable=SC1090
source "$DAOS_SRC_DIR/.build_vars.sh"
if [[ -d "${SL_PREFIX:-}" ]]; then
_py_ver=$(python3 -c "import sys; print(f'python{sys.version_info.major}.{sys.version_info.minor}')" 2>/dev/null)
for _py_path in \
"${SL_PREFIX}/lib64/${_py_ver}/site-packages" \
"${SL_PREFIX}/lib/${_py_ver}/site-packages"   \
"${SL_PREFIX}/lib/daos/python"
do
if [[ -d "$_py_path" ]]; then
echo "[INFO] setup-python.sh: adding install path → $_py_path"
PYTHONPATH="${PYTHONPATH:+$PYTHONPATH:}$_py_path"
fi
done
unset _py_ver _py_path
else
echo "setup-python.sh: [INFO] SL_PREFIX=${SL_PREFIX:-<unset>} does not exist -- skipping install-tree Python paths." >&2
fi
else
echo "setup-python.sh: [INFO] no build yet -- skipping install-tree Python paths (source-tree PYTHONPATH above is still set)." >&2
_daos_build_hint
fi
export PYTHONPATH
unset -f _daos_build_hint

echo "[INFO] setup-python.sh: ready"
echo "  SL_PREFIX  : ${SL_PREFIX:-<unset>}"
echo "  PYTHONPATH : $PYTHONPATH"
