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

	exec "$DTX_UT_BIN" "\$@"
	# exec gdb --args "$DTX_UT_BIN" "\$@"

	# ulimit -n 1024
	# exec env DAOS_ON_VALGRIND=1 $VALGRIND_BIN $VALGRIND_OPTS "$DTX_UT_BIN" "\$@"
	EOF
} | ssh "$BUILD_NODE" bash -l -s -- "$@" |& tee "$CWD/dtx_ut.log"
