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

	if mountpoint -q "$DDB_TESTS_MNT_PATH" ; then
		echo "[INFO] umount $DDB_TESTS_MNT_PATH"
		sudo umount "$DDB_TESTS_MNT_PATH"
	fi

	echo "[INFO] creating mount point $DDB_TESTS_MNT_PATH"
	sudo mkdir -p "$DDB_TESTS_MNT_PATH"
	sudo mount $DDB_TESTS_MNT_OPTS tmpfs "$DDB_TESTS_MNT_PATH"

	exec env PMEMOBJ_CONF=sds.at_create=0 "$DDB_TESTS_BIN" "\$@"
	# exec env PMEMOBJ_CONF=sds.at_create=0 gdb --args "$DDB_TESTS_BIN" "\$@"

	# ulimit -n 1024
	# exec env PMEMOBJ_CONF=sds.at_create=0 DAOS_ON_VALGRIND=1 $VALGRIND_BIN $VALGRIND_OPTS "$DDB_TESTS_BIN" "\$@"
	EOF
} | ssh "$BUILD_NODE" bash -l -s -- "$@" |& tee "$CWD/ddb_tests.log"
