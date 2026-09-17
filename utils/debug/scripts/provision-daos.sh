#!/bin/bash
# Provisions the cluster for a DAOS ticket: runs the DAOS ftest ansible
# playbook (always utils/ansible/ftest/ftest.yml in $DAOS_TOOLS_DIR --
# hardcoded, never given on the command line) against a ticket's own
# inventory.yml, generating that ticket's daos-make.sh/daos-launch.sh (see
# build-daos.sh). By default passes daos_client_manage_system_paths=false
# (as real JSON, not plain key=value -- see below), so this doesn't take
# over another ticket's system-wide ld.so.conf.d/PAM PATH (see
# daos_client_manage_system_paths in utils/ansible/ftest/README.md) --
# build-daos.sh --activate is the explicit, deliberate way to do that.
#
# Usage:
#   provision-daos.sh [inventory-path] [extra ansible-playbook flags...]
#
# inventory-path is a plain, OPTIONAL argument -- not ansible-playbook's own
# -i/--inventory flag (those are rejected here; ansible-playbook is a
# different program, this wrapper is not a passthrough for its inventory
# flag). If omitted, defaults to this ticket's own inventory.yml, next to
# wherever this script itself was invoked from (i.e. the per-ticket
# symlink's directory) -- the same as today, e.g.:
#   cd ~/work/tickets/daos-jira/DAOS-19577 && ./provision-daos.sh --check
# Give it explicitly to run against a specific ticket regardless of
# invocation path, e.g. via this script's canonical daos-tools location:
#   provision-daos.sh ~/work/tickets/daos-jira/DAOS-19577/inventory.yml --check
# The resolved inventory path (explicit or defaulted) must exist as a
# regular file -- checked before ansible-playbook is invoked. Only one
# inventory-path argument is accepted; the ftest.yml playbook itself can
# never be given (a second bare argument is rejected).
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

# The ftest.yml playbook is always $DAOS_TOOLS_DIR/utils/ansible/ftest/ftest.yml
# (hardcoded below, never given on the command line -- rejected if you try).
# ansible-playbook's argparse cannot merge two separate, non-contiguous
# occurrences of its "playbook [playbook ...]" positional, so a caller-
# supplied second bare argument (or any other unrecognized argument) doesn't
# just get ignored -- it makes ansible-playbook itself fail with its own
# large, confusing usage/help dump. To avoid that, validate "$@" against the
# actual ansible-playbook flag set first (from this host's `ansible-playbook
# --help`, ansible-core 2.19), forward only known-good flags, and capture at
# most one bare (non-flag) argument as the inventory-path override; anything
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

_provision_daos_inventory=""
_provision_daos_forward_args=()
_provision_daos_expect_value=0
for _provision_daos_arg in "$@"; do
	if [[ "$_provision_daos_expect_value" -eq 1 ]]; then
		_provision_daos_expect_value=0
		_provision_daos_forward_args+=("$_provision_daos_arg")
		continue
	fi
	case "$_provision_daos_arg" in
	-i | --inventory | --inventory-file | --inventory=* | --inventory-file=*)
		echo "provision-daos.sh: [ERROR] pass the inventory path as a plain argument instead of -i/--inventory, e.g. 'provision-daos.sh /path/to/inventory.yml'." >&2
		exit 1
		;;
	--*=*)
		if _provision_daos_contains "${_provision_daos_arg%%=*}" "${_provision_daos_value_flags[@]}"; then
			_provision_daos_forward_args+=("$_provision_daos_arg")
			continue
		fi
		echo "provision-daos.sh: [ERROR] unrecognized argument '$_provision_daos_arg' -- refusing to forward it to ansible-playbook (see this script's Usage comment for the supported flags)." >&2
		exit 1
		;;
	-*)
		if _provision_daos_contains "$_provision_daos_arg" "${_provision_daos_novalue_flags[@]}"; then
			_provision_daos_forward_args+=("$_provision_daos_arg")
			continue
		fi
		if _provision_daos_contains "$_provision_daos_arg" "${_provision_daos_value_flags[@]}"; then
			_provision_daos_expect_value=1
			_provision_daos_forward_args+=("$_provision_daos_arg")
			continue
		fi
		echo "provision-daos.sh: [ERROR] unrecognized argument '$_provision_daos_arg' -- refusing to forward it to ansible-playbook (only additional ansible-playbook flags are accepted; see this script's Usage comment)." >&2
		exit 1
		;;
	*)
		if [[ -n "$_provision_daos_inventory" ]]; then
			echo "provision-daos.sh: [ERROR] only one inventory-path argument is allowed (already got '$_provision_daos_inventory', also saw '$_provision_daos_arg') -- the ftest.yml playbook itself can not be given." >&2
			exit 1
		fi
		_provision_daos_inventory="$_provision_daos_arg"
		continue
		;;
	esac
done

if [[ -z "$_provision_daos_inventory" ]]; then
	_provision_daos_inventory="$(realpath "$(dirname "$0")")/inventory.yml"
fi

if [[ ! -f "$_provision_daos_inventory" ]]; then
	echo "provision-daos.sh: [ERROR] inventory file not found: $_provision_daos_inventory" >&2
	exit 1
fi

# Resolve to an absolute path before the cd below -- otherwise a relative
# inventory-path argument (given relative to the caller's cwd) would be
# silently reinterpreted relative to the ftest directory instead.
_provision_daos_inventory="$(realpath "$_provision_daos_inventory")"

cd "${DAOS_TOOLS_DIR:-$HOME/work/daos-tools}/utils/ansible/ftest"
exec ansible-playbook ftest.yml -i "$_provision_daos_inventory" \
	-e '{"daos_client_manage_system_paths": false}' "${_provision_daos_forward_args[@]}"
