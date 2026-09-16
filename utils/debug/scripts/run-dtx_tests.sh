#!/bin/bash

# set -x
set -u -e -o pipefail

CWD="$(realpath "$(dirname "$0")")"

source "$CWD/env.sh"

{
	cat <<-EOF
	# set -x
	set -u -e -o pipefail

	SSH_AGENT_PID=\${SSH_AGENT_PID:-}

	if mountpoint -q "$DTX_TESTS_MNT_PATH" ; then
		echo "[INFO] umount $DTX_TESTS_MNT_PATH"
		sudo umount "$DTX_TESTS_MNT_PATH"
	fi

	echo "[INFO] creating mount point $DTX_TESTS_MNT_PATH"
	sudo mkdir -p "$DTX_TESTS_MNT_PATH"
	sudo mount $DTX_TESTS_MNT_OPTS tmpfs "$DTX_TESTS_MNT_PATH"

	# -S/--storage defaults to this ticket's isolated mount point; a
	# user-supplied -S later in "\$@" still wins (getopt keeps the last).
	exec env PMEMOBJ_CONF=sds.at_create=0 "$DTX_TESTS_BIN" -S "$DTX_TESTS_MNT_PATH" "\$@"
	# exec env PMEMOBJ_CONF=sds.at_create=0 gdb --args "$DTX_TESTS_BIN" -S "$DTX_TESTS_MNT_PATH" "\$@"

	# ulimit -n 1024
	# exec env PMEMOBJ_CONF=sds.at_create=0 DAOS_ON_VALGRIND=1 $VALGRIND_BIN $VALGRIND_OPTS "$DTX_TESTS_BIN" -S "$DTX_TESTS_MNT_PATH" "\$@"
	EOF
} | ssh "$BUILD_NODE" bash -l -s -- "$@" |& tee "$CWD/dtx_tests.log"
