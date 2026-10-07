#!/bin/bash
# Isolated DAOS build of a ticket's worktree on the build node, without
# requiring provision-daos.sh/ansible to have run first.
#
# Does NOT touch the shared workspace (DAOS_SHARED_WORKSPACE, default
# /scratch/$USER/daos-install, i.e. the ~/work/daos deployment): it builds the
# per-ticket worktree into its own BUILD_ROOT/PREFIX and reuses the shared,
# already-built prereqs through the scons ALT_PREFIX mechanism (same approach
# as generate-daos-env.sh / compute-daos-alt-prefix.sh). No system-wide
# activation (daos_server_helper, dfuse, spdk) is performed: only the
# standalone unit-test binaries (ddb_tests/ddb_ut/vos_tests/...) are needed.
#
# The prereqs (.build_vars.sh) and the python virtualenv are taken from the
# ticket's own workspace when provision-daos.sh/build-daos.sh already
# populated it, and from the shared workspace otherwise -- so it works on a
# freshly created ticket worktree whose prefix is still empty.
#
# The ticket ID is derived from this script's own calling directory's
# basename (e.g. ~/work/tickets/daos-jira/DAOS-17321 -> daos-17321), same as
# every other per-ticket script here -- run it from within the ticket
# directory (or via its symlink there), never directly from daos-tools.
#
# Usage: build-isolated.sh [-j N] [--] [extra scons args]
#
# Env overrides:
#   DAOS_SHARED_WORKSPACE  Shared workspace holding the prereqs/virtualenv to
#                          fall back to (default: /scratch/$USER/daos-install)

set -u -e -o pipefail

CWD="$(realpath "$(dirname "$0")")"
source "$CWD/env.sh"

TICKET_LC="$(basename "$CWD" | tr '[:upper:]' '[:lower:]')"
DAOS_SHARED_WORKSPACE="${DAOS_SHARED_WORKSPACE:-/scratch/$USER/daos-install}"

JOBS_NB=16
if [[ "${1:-}" == "-j" ]]; then
	JOBS_NB="$2"
	shift 2
fi
[[ "${1:-}" == "--" ]] && shift

ISO_SRC="$CWD/daos"
ISO_BUILD="/var/tmp/daos-build-$TICKET_LC"
ISO_PREFIX="/scratch/$USER/daos-install-$TICKET_LC/install"
ALT_PREFIX_HELPER="${DAOS_TOOLS_DIR:-$HOME/work/daos-tools}/utils/debug/scripts/compute-daos-alt-prefix.sh"

# Prefer the ticket's own workspace once provisioned/built, else the shared one
# (both live on the build node's /scratch, which is also mounted here).
BUILD_VARS="$DAOS_INSTALL/lib/daos/.build_vars.sh"
[[ -r "$BUILD_VARS" ]] || BUILD_VARS="$DAOS_SHARED_WORKSPACE/install/lib/daos/.build_vars.sh"
VENV="$DAOS_WORKSPACE/virtualenvs"
[[ -r "$VENV/bin/activate" ]] || VENV="$DAOS_SHARED_WORKSPACE/virtualenvs"
[[ -r "$BUILD_VARS" ]] || { echo "[ERROR] no prereqs found: $BUILD_VARS" >&2; exit 1; }
[[ -r "$VENV/bin/activate" ]] || { echo "[ERROR] no python virtualenv found: $VENV" >&2; exit 1; }
echo "[INFO] prereqs from $BUILD_VARS, virtualenv $VENV"

{
	cat <<-EOF
	set -u -e -o pipefail
	SSH_AGENT_PID=\${SSH_AGENT_PID:-}

	alt_prefix="\$("$ALT_PREFIX_HELPER" "$BUILD_VARS")"
	[[ -n "\$alt_prefix" ]] || { echo "[ERROR] empty ALT_PREFIX" >&2; exit 1; }
	echo "[INFO] ALT_PREFIX=\$alt_prefix"

	source "$VENV/bin/activate"
	mkdir -p "$ISO_BUILD" "$ISO_PREFIX"
	cd "$ISO_SRC"
	echo "[INFO] building \$(git rev-parse --short HEAD) from $ISO_SRC into $ISO_BUILD (PREFIX=$ISO_PREFIX)"
	env --unset=http_proxy --unset=https_proxy --unset=HTTP_PROXY --unset=HTTPS_PROXY \\
		MPI_PKG=any scons --directory="$ISO_SRC" --jobs=$JOBS_NB \\
		BUILD_TYPE=debug BUILD_ROOT="$ISO_BUILD/daos" PREFIX="$ISO_PREFIX" \\
		ALT_PREFIX="\$alt_prefix" "\$@"
	scons --directory="$ISO_SRC" --jobs=$JOBS_NB install
	ls -la "$ISO_PREFIX/bin/ddb_tests" "$ISO_PREFIX/bin/ddb_ut" "$ISO_PREFIX/bin/vos_tests"
	echo "[INFO] build done"
	EOF
} | ssh "$BUILD_NODE" bash -l -s -- "$@" |& tee "$CWD/build-isolated.log"
