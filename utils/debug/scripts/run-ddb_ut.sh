#!/bin/bash
#
# Flags (both optional, consumed locally before ssh -- everything else is
# forwarded to the ddb_ut binary as-is):
#   --prefix DIR   Exec DIR/bin/ddb_ut instead of $DDB_UT_BIN (e.g.
#                  build-isolated.sh's own isolated install prefix).
#   --valgrind     Run under $VALGRIND_BIN/$VALGRIND_OPTS (also lowers the fd
#                  limit to 1024 via prlimit -- valgrind aborts with "Private
#                  file creation failed" against this cluster's much higher
#                  default). Logs to ddb_ut-valgrind.log instead of ddb_ut.log.

# set -x
set -u -e -o pipefail

CWD="$(realpath "$(dirname "$0")")"

source "$CWD/env.sh"

PREFIX=""
VALGRIND=0
ARGS=()
while [[ $# -gt 0 ]]; do
	case "$1" in
		--prefix) PREFIX="$2"; shift 2 ;;
		--valgrind) VALGRIND=1; shift ;;
		*) ARGS+=("$1"); shift ;;
	esac
done
set -- "${ARGS[@]}"

BIN="$DDB_UT_BIN"
[[ -n "$PREFIX" ]] && BIN="$PREFIX/bin/ddb_ut"

RUN_PREFIX=""
LOG="$CWD/ddb_ut.log"
if [[ "$VALGRIND" -eq 1 ]]; then
	RUN_PREFIX="prlimit --nofile=1024:1024 env DAOS_ON_VALGRIND=1 $VALGRIND_BIN $VALGRIND_OPTS"
	LOG="$CWD/ddb_ut-valgrind.log"
fi

{
	cat <<-EOF
	# set -x
	set -u -e -o pipefail

	SSH_AGENT_PID=\${SSH_AGENT_PID:-}

	exec $RUN_PREFIX "$BIN" "\$@"
	# exec gdb --args "$BIN" "\$@"
	EOF
} | ssh "$BUILD_NODE" bash -l -s -- "$@" |& tee "$LOG"
