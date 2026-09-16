#!/bin/bash
# Builds/installs DAOS for one ticket by ssh-ing to $BUILD_NODE and invoking
# that ticket's own daos-make.sh (generated into $DAOS_WORKSPACE/daos-make.sh
# by "ansible-playbook -i inventory.yml ftest.yml" -- run that yourself
# first; this script does not do it for you). Reuses shared prereqs via the
# ticket's daos_alt_prefix (see env.sh/inventory.yml, generate-daos-env.sh).
#
# By default passes --build-only to daos-make.sh, so this build/install goes
# only into the ticket's own isolated prefix and does NOT take over
# daos_server_helper/dfuse/ld.so.conf.d/systemd units on the shared cluster
# (i.e. it won't disturb whichever ticket is currently the "live" one). Pass
# --activate to drop --build-only and make this ticket's build the live one.
#
# Usage:
#   build-daos.sh [--activate] [-j|--jobs N] [daos-make.sh flags...]
#
# Examples:
#   build-daos.sh --force --deps      # full (re)build, isolated (was install-daos.sh)
#   build-daos.sh                     # incremental rebuild, isolated (was update-daos.sh)
#   build-daos.sh --activate --force --deps  # full rebuild AND make it the live ticket
#
# All other flags (--force, --deps, --jobs, --build-type, --mpich, ...) are
# forwarded as-is to daos-make.sh -- see `daos-make.sh --help` on $BUILD_NODE,
# or utils/ansible/ftest/roles/daos_dev/templates/daos-make.sh.j2 in daos-tools.

set -u -e -o pipefail

CWD="$(realpath "$(dirname "$0")")"

source "$CWD/env.sh"

ACTIVATE=0
ARGS=()
for arg in "$@"; do
if [[ "$arg" == "--activate" ]]; then
ACTIVATE=1
else
ARGS+=("$arg")
fi
done

if [[ "$ACTIVATE" -eq 1 ]]; then
echo "[INFO] --activate given: this build WILL take over daos_server_helper/dfuse/ld.so.conf.d/systemd units on the shared cluster"
else
ARGS=("--build-only" "${ARGS[@]}")
fi

DAOS_MAKE_SH="$DAOS_WORKSPACE/daos-make.sh"

{
	cat <<-EOF
	set -u -e -o pipefail

	SSH_AGENT_PID=\${SSH_AGENT_PID:-}

	if [[ ! -x "$DAOS_MAKE_SH" ]] ; then
		echo "[ERROR] $DAOS_MAKE_SH not found or not executable." >&2
		echo "  Run 'ansible-playbook -i inventory.yml ftest.yml' first (from daos-tools/utils/ansible/ftest/) to generate it for this ticket." >&2
		exit 1
	fi

	exec "$DAOS_MAKE_SH" ${ARGS[@]@Q}
	EOF
} | ssh "$BUILD_NODE" bash -l -s |& tee "$CWD/build-daos.log"
