#!/bin/bash
#
# Run DAOS unit-test suites (utils/run_utest.py, the driver behind the CI
# "Unit Test" stages, suites listed in src/*/utils/utest.yaml) on $BUILD_NODE
# from this ticket's source tree ($DAOS_SRC), with its isolated install prefix.
#
# run_utest.py needs root (it mounts tmpfs / creates AIO files), a python
# virtualenv providing junit_xml, and a lot of /tmp space for the AIO suites
# (VOS_md_on_ssd and friends: one 4 GB AIO file per suite entry, leaked fds
# keep the previous ones alive). This wrapper handles all three.
#
# Flags (all optional, consumed locally before ssh -- everything else is
# forwarded to run_utest.py as-is, e.g. --suite_filter/--test_filter/--memcheck):
#   --private-mount   Run inside a private mount namespace (sudo unshare -m):
#                      a private tmpfs on $UTEST_MNT_PATH (default /mnt/daos,
#                      $UTEST_MNT_OPTS) and a private tmpfs on /tmp
#                      ($UTEST_TMP_SIZE, default 34G, enough for two AIO
#                      files) so that neither collides with anything else on
#                      the shared host (another ticket's live daos_server, a
#                      filled /tmp). Both vanish with the process.
#   --log-dir DIR      run_utest.py --log_dir (default
#                      /var/tmp/daos_utest_<ticket>_<timestamp>). Must be
#                      outside /tmp when --private-mount is used (the private
#                      /tmp disappears with the process).
#   --env VAR=VAL      Environment variable for the test processes (repeatable,
#                      e.g. --env D_LOG_MASK=WARN). Passed through the inner
#                      env: sudo's default policy drops the caller's variables.
#   --venv DIR         Python virtualenv to activate (default: $DAOS_PY_VENV
#                      from env.sh if set, else the ticket workspace's
#                      virtualenvs, else $DAOS_SHARED_WORKSPACE/virtualenvs --
#                      same lookup as build-isolated.sh).
#
# No --prefix: run_utest.py takes PREFIX/BUILD_DIR from $DAOS_SRC/.build_vars.json,
# i.e. the install of the last scons run from this tree (build-isolated.sh's
# isolated prefix), and mounts its AIO tmpfs on the hard-coded /mnt/daos.
#
# Examples:
#   ./run-utest.sh --private-mount --suite_filter VOS_md_on_ssd
#   ./run-utest.sh --private-mount --suite_filter VOS_md_on_ssd --test_filter bio_ut \
#       --env D_LOG_MASK=WARN --log-dir /var/tmp/daos_utest_mine

# set -x
set -u -e -o pipefail

CWD="$(realpath "$(dirname "$0")")"

source "$CWD/env.sh"

TICKET_LC="$(basename "$CWD" | tr '[:upper:]' '[:lower:]')"
DAOS_SHARED_WORKSPACE="${DAOS_SHARED_WORKSPACE:-/scratch/$USER/daos-install}"

PRIVATE_MOUNT=0
LOG_DIR=""
VENV="${DAOS_PY_VENV:-}"
ENV_VARS=()
ARGS=()
while [[ $# -gt 0 ]]; do
	case "$1" in
		--private-mount) PRIVATE_MOUNT=1; shift ;;
		--log-dir) LOG_DIR="$2"; shift 2 ;;
		--env) ENV_VARS+=("$2"); shift 2 ;;
		--venv) VENV="$2"; shift 2 ;;
		-h|--help)
			sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
			exit 0 ;;
		*) ARGS+=("$1"); shift ;;
	esac
done
set -- "${ARGS[@]}"

[[ -n "$LOG_DIR" ]] || LOG_DIR="/var/tmp/daos_utest_${TICKET_LC}_$(date +%Y%m%d-%H%M%S)"
if [[ "$PRIVATE_MOUNT" -eq 1 && "$LOG_DIR" == /tmp/* ]]; then
	echo "[ERROR] --log-dir $LOG_DIR is under /tmp, which is private (and lost) with --private-mount" >&2
	exit 1
fi

# Same virtualenv lookup as build-isolated.sh (/scratch is mounted here too).
if [[ -z "$VENV" ]]; then
	VENV="$DAOS_WORKSPACE/virtualenvs"
	[[ -r "$VENV/bin/activate" ]] || VENV="$DAOS_SHARED_WORKSPACE/virtualenvs"
fi
[[ -r "$VENV/bin/activate" ]] || { echo "[ERROR] no python virtualenv found: $VENV" >&2; exit 1; }

UTEST_MNT_PATH="${UTEST_MNT_PATH:-/mnt/daos}"
UTEST_MNT_OPTS="${UTEST_MNT_OPTS:--t tmpfs -o rw,noatime,size=32G}"
UTEST_TMP_SIZE="${UTEST_TMP_SIZE:-34G}"

# The variables for the test processes: PATH (expanded by the remote shell once
# the virtualenv is activated) plus the user's --env ones, applied by an inner
# `env` after the sudo hops (sudo resets PATH and drops the caller's variables).
# shellcheck disable=SC2016  # expanded by the remote shell, not here
INNER_ENV='PATH=$PATH'
for kv in "${ENV_VARS[@]+"${ENV_VARS[@]}"}"; do
	INNER_ENV="$INNER_ENV $kv"
done

LOG="$CWD/utest.log"
echo "[INFO] run_utest.py $* (log dir $LOG_DIR on $BUILD_NODE, virtualenv $VENV)"

{
	cat <<-EOF
	# set -x
	set -u -e -o pipefail

	SSH_AGENT_PID=\${SSH_AGENT_PID:-}

	cd "$DAOS_SRC"
	source "$VENV/bin/activate"
	mkdir -p "$LOG_DIR"

	if [[ "$PRIVATE_MOUNT" -eq 1 ]] ; then
		echo "[INFO] private tmpfs on $UTEST_MNT_PATH and /tmp ($UTEST_TMP_SIZE)"
		exec sudo -n unshare -m --propagation private -- /bin/sh -c \\
			"mkdir -p $UTEST_MNT_PATH && mount $UTEST_MNT_OPTS tmpfs $UTEST_MNT_PATH && \\
			 mount -t tmpfs -o size=$UTEST_TMP_SIZE tmpfs /tmp && \\
			 exec sudo -n -u $USER -E env $INNER_ENV ./utils/run_utest.py --log_dir $LOG_DIR \\"\\\$@\\"" \\
			sh "\$@"
	fi

	# run_utest.py runs as the caller and sudo's the suites that need it, like CI
	exec env $INNER_ENV ./utils/run_utest.py --log_dir "$LOG_DIR" "\$@"
	EOF
} | ssh "$BUILD_NODE" bash -l -s -- "$@" |& tee "$LOG"
