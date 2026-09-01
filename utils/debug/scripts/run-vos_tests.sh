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

	if mountpoint -q "$VOS_TESTS_MNT_PATH" ; then
		echo "[INFO] umount $VOS_TESTS_MNT_PATH"
		sudo umount "$VOS_TESTS_MNT_PATH"
	fi

	echo "[INFO] creating mount point $VOS_TESTS_MNT_PATH"
	sudo mkdir -p "$VOS_TESTS_MNT_PATH"
	sudo mount $VOS_TESTS_MNT_OPTS tmpfs "$VOS_TESTS_MNT_PATH"

	exec env PMEMOBJ_CONF=sds.at_create=0 "$VOS_TESTS_BIN" "\$@"
	# exec env PMEMOBJ_CONF=sds.at_create=0 gdb --args "$VOS_TESTS_BIN" "\$@"

	# ulimit -n 1024
	# exec env PMEMOBJ_CONF=sds.at_create=0 DAOS_ON_VALGRIND=1 $VALGRIND_BIN $VALGRIND_OPTS "$VOS_TESTS_BIN" "\$@"
	EOF
} | ssh "$BUILD_NODE" bash -l -s -- "$@" |& tee "$CWD/vos_tests.log"
