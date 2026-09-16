#!/bin/bash
# Provisions the cluster for this ticket: runs the DAOS ftest ansible
# playbook against this ticket's own inventory.yml, generating this
# ticket's daos-make.sh/daos-launch.sh (see build-daos.sh). By default
# passes -e daos_client_manage_system_paths=false, so this doesn't take
# over another ticket's system-wide ld.so.conf.d/PAM PATH (see
# daos_client_manage_system_paths in utils/ansible/ftest/README.md) --
# build-daos.sh --activate is the explicit, deliberate way to do that.
#
# Usage:
#   provision-daos.sh [ansible-playbook flags...]
#
# Review inventory.yml before running this: it touches the shared
# brd-216..219 cluster (packages, users/groups, limits, hugepages, etc.).
#
# A user-supplied -e daos_client_manage_system_paths=true later in "$@"
# still wins (ansible-playbook keeps the last --extra-vars for a given key).

set -u -e -o pipefail

CWD="$(realpath "$(dirname "$0")")"

cd "${DAOS_TOOLS_DIR:-$HOME/work/daos-tools}/utils/ansible/ftest"
exec ansible-playbook -i "$CWD/inventory.yml" ftest.yml \
	-e daos_client_manage_system_paths=false "$@"
