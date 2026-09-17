#!/bin/bash
# Provisions the cluster for this ticket: runs the DAOS ftest ansible
# playbook against this ticket's own inventory.yml, generating this
# ticket's daos-make.sh/daos-launch.sh (see build-daos.sh). By default
# passes daos_client_manage_system_paths=false (as real JSON, not plain
# key=value -- see below), so this doesn't take over another ticket's
# system-wide ld.so.conf.d/PAM PATH (see daos_client_manage_system_paths
# in utils/ansible/ftest/README.md) -- build-daos.sh --activate is the
# explicit, deliberate way to do that.
#
# Usage:
#   provision-daos.sh [extra ansible-playbook flags...]
#
# This ticket's own inventory.yml and the shared ftest.yml playbook are
# already hardcoded below -- do NOT pass -i/--inventory or a playbook path
# yourself (rejected by the argument validation below). Only pass
# *additional* ansible-playbook flags here, e.g.:
#   provision-daos.sh -e daos_client_manage_system_paths=true
#   provision-daos.sh --check --diff
#   provision-daos.sh -vvv --limit brd-217
#
# Review inventory.yml before running this: it touches the shared
# brd-216..219 cluster (packages, users/groups, limits, hugepages, etc.).
#
# A user-supplied -e daos_client_manage_system_paths=true (or as JSON)
# later in "$@" still wins (ansible-playbook keeps the last --extra-vars
# for a given key).
#
# Uses JSON extra-vars ('{"key": false}'), not plain key=value: the plain
# form always produces a *string*, and ansible-core's strict when:
# conditionals reject a truthy non-empty string like "false" outright
# (manage_system_paths.yml's own when: also defends against this with
# | bool, but this avoids relying on that alone).

set -u -e -o pipefail

CWD="$(realpath "$(dirname "$0")")"

# Both -i/inventory and the ftest.yml playbook are already hardcoded in the
# exec below. ansible-playbook's argparse cannot merge two separate,
# non-contiguous occurrences of its "playbook [playbook ...]" positional, so
# a caller-supplied -i/playbook (or any other unrecognized argument) doesn't
# just get ignored -- it makes ansible-playbook itself fail with its own
# large, confusing usage/help dump. To avoid that, validate "$@" against the
# actual ansible-playbook flag set first (from this host's `ansible-playbook
# --help`, ansible-core 2.19) and only forward known-good flags; anything
# else is rejected here with a clear message instead.
_provision_daos_novalue_flags=(
	-h --help --version
	-v -vv -vvv -vvvv -vvvvv -vvvvvv
	-k --ask-pass --force-handlers -b --become -K --ask-become-pass
	-C --check -D --diff --list-hosts --flush-cache
	-J --ask-vault-password --ask-vault-pass
	--syntax-check --list-tasks --list-tags --step
)
_provision_daos_value_flags=(
	--private-key --key-file -u --user -c --connection -T --timeout
	--ssh-common-args --sftp-extra-args --scp-extra-args --ssh-extra-args
	--connection-password-file --become-method --become-user
	--become-password-file --become-pass-file
	-t --tags --skip-tags -l --limit -e --extra-vars --vault-id
	--vault-password-file --vault-pass-file -f --forks -M --module-path
	--start-at-task
)

_provision_daos_contains() {
	local needle="$1"
	shift
	local candidate
	for candidate in "$@"; do
		[[ "$candidate" == "$needle" ]] && return 0
	done
	return 1
}

_provision_daos_expect_value=0
for _provision_daos_arg in "$@"; do
	if [[ "$_provision_daos_expect_value" -eq 1 ]]; then
		_provision_daos_expect_value=0
		continue
	fi
	case "$_provision_daos_arg" in
	-i | --inventory | --inventory-file | --inventory=* | --inventory-file=*)
		echo "provision-daos.sh: [ERROR] do not pass -i/--inventory yourself -- this ticket's own $CWD/inventory.yml is already used internally." >&2
		exit 1
		;;
	--*=*)
		if _provision_daos_contains "${_provision_daos_arg%%=*}" "${_provision_daos_value_flags[@]}"; then
			continue
		fi
		echo "provision-daos.sh: [ERROR] unrecognized argument '$_provision_daos_arg' -- refusing to forward it to ansible-playbook (see this script's Usage comment for the supported flags)." >&2
		exit 1
		;;
	esac
	if _provision_daos_contains "$_provision_daos_arg" "${_provision_daos_novalue_flags[@]}"; then
		continue
	fi
	if _provision_daos_contains "$_provision_daos_arg" "${_provision_daos_value_flags[@]}"; then
		_provision_daos_expect_value=1
		continue
	fi
	echo "provision-daos.sh: [ERROR] unrecognized argument '$_provision_daos_arg' -- refusing to forward it to ansible-playbook (only additional ansible-playbook flags are accepted; see this script's Usage comment)." >&2
	exit 1
done

cd "${DAOS_TOOLS_DIR:-$HOME/work/daos-tools}/utils/ansible/ftest"
exec ansible-playbook -i "$CWD/inventory.yml" ftest.yml \
	-e '{"daos_client_manage_system_paths": false}' "$@"
