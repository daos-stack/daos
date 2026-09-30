#!/bin/bash
#
# Flags (all optional, consumed locally before ssh -- everything else is
# forwarded to the ddb_tests binary as-is):
#   --private-mount   Run inside a private mount namespace (sudo unshare -m)
#                      so the tmpfs mount is invisible to and can't collide
#                      with anything else on the shared host (e.g. another
#                      ticket's live daos_server). Vanishes with the process,
#                      no manual cleanup needed.
#   --prefix DIR       Exec DIR/bin/ddb_tests instead of $DDB_TESTS_BIN (e.g.
#                      build-isolated.sh's own isolated install prefix).
#   --valgrind         Run under $VALGRIND_BIN/$VALGRIND_OPTS (also lowers
#                      the fd limit to 1024 via prlimit -- valgrind aborts
#                      with "Private file creation failed" against this
#                      cluster's much higher default). Logs to
#                      ddb_tests-valgrind.log instead of ddb_tests.log.

# set -x
set -u -e -o pipefail

CWD="$(realpath "$(dirname "$0")")"

source "$CWD/env.sh"

PRIVATE_MOUNT=0
PREFIX=""
VALGRIND=0
ARGS=()
while [[ $# -gt 0 ]]; do
	case "$1" in
		--private-mount) PRIVATE_MOUNT=1; shift ;;
		--prefix) PREFIX="$2"; shift 2 ;;
		--valgrind) VALGRIND=1; shift ;;
		*) ARGS+=("$1"); shift ;;
	esac
done
set -- "${ARGS[@]}"

BIN="$DDB_TESTS_BIN"
[[ -n "$PREFIX" ]] && BIN="$PREFIX/bin/ddb_tests"

RUN_PREFIX="env PMEMOBJ_CONF=sds.at_create=0"
LOG="$CWD/ddb_tests.log"
if [[ "$VALGRIND" -eq 1 ]]; then
	RUN_PREFIX="prlimit --nofile=1024:1024 env PMEMOBJ_CONF=sds.at_create=0 DAOS_ON_VALGRIND=1 $VALGRIND_BIN $VALGRIND_OPTS"
	LOG="$CWD/ddb_tests-valgrind.log"
fi

{
	cat <<-EOF
	# set -x
	set -u -e -o pipefail

	SSH_AGENT_PID=\${SSH_AGENT_PID:-}

	if [[ "$PRIVATE_MOUNT" -eq 1 ]] ; then
		echo "[INFO] running $BIN \$* (private tmpfs on $DDB_TESTS_MNT_PATH)"
		exec sudo -n unshare -m --propagation private -- /bin/sh -c \\
			'mkdir -p $DDB_TESTS_MNT_PATH && mount $DDB_TESTS_MNT_OPTS tmpfs $DDB_TESTS_MNT_PATH && exec sudo -n -u $USER $RUN_PREFIX $BIN "\$@"' \\
			sh "\$@"
	fi

	if mountpoint -q "$DDB_TESTS_MNT_PATH" ; then
		echo "[INFO] umount $DDB_TESTS_MNT_PATH"
		sudo umount "$DDB_TESTS_MNT_PATH"
	fi

	echo "[INFO] creating mount point $DDB_TESTS_MNT_PATH"
	sudo mkdir -p "$DDB_TESTS_MNT_PATH"
	sudo mount $DDB_TESTS_MNT_OPTS tmpfs "$DDB_TESTS_MNT_PATH"

	exec $RUN_PREFIX "$BIN" "\$@"
	# exec env PMEMOBJ_CONF=sds.at_create=0 gdb --args "$BIN" "\$@"
	EOF
} | ssh "$BUILD_NODE" bash -l -s -- "$@" |& tee "$LOG"
