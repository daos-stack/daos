#!/bin/bash
# Runs DAOS functional tests (avocado) for one ticket through that ticket's
# generated daos-launch.sh on $LOGIN_NODE (rendered into $DAOS_WORKSPACE by
# provision-daos.sh), after installing the ticket's ./files/ftest/ overlay on
# top of $DAOS_INSTALL/lib/daos/TESTING/ftest/ so the python/yaml of a test
# under development can be iterated on without rebuilding.
#
# Usage:
#   run-ftest.sh [OPTIONS] [TEST...] [-- LAUNCH_ARGS...]
#
# TEST is any launch.py test filter, e.g. "PoolCreateSlowSvc",
# "DmgPoolQueryRanks,test_pool_query_ranks_basic",
# "DaosCoreTest,test_daos_management". Without TEST, FTEST_TESTS from env.sh
# is used; when that is empty too, the usage is printed and the script exits 1.
# Everything after "--" is forwarded to launch.py as-is (e.g. -- --repeat 3).
#
# Options (each defaults to the FTEST_* variable of env.sh; env.sh is sourced,
# so override with these options rather than with exported variables):
#   --servers NODESET  test servers (FTEST_SERVERS, else SERVER_NODES)
#   --clients NODESET  test clients (FTEST_CLIENTS, else CLIENT_NODES when it is
#                      unset); an empty value omits --test_clients
#   --nvme MODE        launch.py --nvme (FTEST_NVME): auto, auto_md_on_ssd,
#                      auto_nvme, ...; an empty value omits it (ram only; the
#                      auto modes need the spdk-tools setup.sh on the servers)
#   --scm-size N       launch.py --scm_size in GiB (FTEST_SCM_SIZE); needed by
#                      "class: ram" yamls without scm_size, which otherwise
#                      auto-size a ramdisk from the whole node memory (249 GiB)
#   --provider P       launch.py --provider (FTEST_PROVIDER, else ofi+tcp)
#   --no-overlay       do not install ./files/ftest/ onto the install tree
#   --log FILE         output log, truncated per run (default: ./ftest.log)
#   --skip-live-check  do not verify that this ticket's install is the live one
#                      (also FTEST_SKIP_LIVE_CHECK=1 in env.sh)
#   --dry-run          print the live-install check result, the overlay plan and
#                      the composed launch command, without running anything
#   -h, --help         this help
#
# Requires build-daos.sh --activate first: ftest drives the cluster-wide
# daos_server/daos_agent systemd units, which are exclusive across tickets.
# Before doing anything, the script checks on $LOGIN_NODE, the test servers
# and the test clients that this ticket's $DAOS_INSTALL is the live one:
# /etc/ld.so.conf.d/daos-x86_64.conf (repointed by build-daos.sh --activate)
# and the ExecStart of the daos_server/daos_agent systemd units (rendered by
# provision-daos.sh) must both resolve into $DAOS_INSTALL, otherwise it
# refuses to run (another ticket is live, or this one was only provisioned
# and not activated yet).
# The avocado job results are written on $LOGIN_NODE under
# ~/avocado/job-results/launch/functional_manual/ (job.log, per-test logs,
# engine/server logs collected by launch.py).
#
# Exit code: the one of daos-launch.sh/launch.py (0 when every test passed).

# set -x
set -u -e -o pipefail

CWD="$(realpath "$(dirname "$0")")"

source "$CWD/env.sh"

usage() {
	sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
	echo ">>> [ERROR] $1" >&2
	exit "${2:-1}"
}

# Split "[OPTIONS] [TEST...] -- [LAUNCH_ARGS...]" at the first "--": getopt
# would otherwise swallow the marker and merge LAUNCH_ARGS into the tests.
ARGS=()
LAUNCH_ARGS=()
while [[ $# -gt 0 ]] ; do
	if [[ "$1" == "--" ]] ; then
		shift
		LAUNCH_ARGS=("$@")
		break
	fi
	ARGS+=("$1")
	shift
done

SERVERS="${FTEST_SERVERS:-$SERVER_NODES}"
CLIENTS="${FTEST_CLIENTS-$CLIENT_NODES}"
NVME="${FTEST_NVME:-}"
SCM_SIZE="${FTEST_SCM_SIZE:-}"
PROVIDER="${FTEST_PROVIDER:-ofi+tcp}"
OVERLAY=true
LOG="$CWD/ftest.log"
LIVE_CHECK=true
[[ "${FTEST_SKIP_LIVE_CHECK:-0}" != "1" ]] || LIVE_CHECK=false
DRY_RUN=false

OPTIONS=$(getopt -o "h" --long "servers:,clients:,nvme:,scm-size:,provider:,no-overlay,log:,skip-live-check,dry-run,help" -- "${ARGS[@]}") || exit 1
eval set -- "$OPTIONS"
while true ; do
	case "$1" in
		--servers) SERVERS="$2" ; shift 2 ;;
		--clients) CLIENTS="$2" ; shift 2 ;;
		--nvme) NVME="$2" ; shift 2 ;;
		--scm-size) SCM_SIZE="$2" ; shift 2 ;;
		--provider) PROVIDER="$2" ; shift 2 ;;
		--no-overlay) OVERLAY=false ; shift 1 ;;
		--log) LOG="$2" ; shift 2 ;;
		--skip-live-check) LIVE_CHECK=false ; shift 1 ;;
		--dry-run) DRY_RUN=true ; shift 1 ;;
		-h|--help) usage ; exit 0 ;;
		--) shift ; break ;;
		*) die "unrecognized option $1" ;;
	esac
done

TESTS=("$@")
if [[ ${#TESTS[@]} -eq 0 ]] ; then
	# FTEST_TESTS holds space-separated launch.py filters: split on purpose.
	read -r -a TESTS <<< "${FTEST_TESTS:-}"
fi
if [[ ${#TESTS[@]} -eq 0 ]] ; then
	usage >&2
	die "no test given and FTEST_TESTS is empty in $CWD/env.sh"
fi
[[ -n "$SERVERS" ]] || die "no test servers: use --servers or set FTEST_SERVERS/SERVER_NODES in env.sh"

if [[ -n "$CLIENTS" ]] && [[ $(nodeset -c "$SERVERS" -i "$CLIENTS") -gt 0 ]] ; then
	echo ">>> [WARN] test servers ($SERVERS) and test clients ($CLIENTS) overlap: $(nodeset -f "$SERVERS" -i "$CLIENTS")" >&2
fi

LAUNCH=(-v --failfast --disable_stop_daos --mode manual)
[[ -z "$NVME" ]] || LAUNCH+=(--nvme "$NVME")
[[ -z "$SCM_SIZE" ]] || LAUNCH+=(--scm_size "$SCM_SIZE")
LAUNCH+=(--verbose --provider "$PROVIDER" --test_servers "$SERVERS")
[[ -z "$CLIENTS" ]] || LAUNCH+=(--test_clients "$CLIENTS")
LAUNCH+=("${LAUNCH_ARGS[@]}" "${TESTS[@]}")

FTEST_SOURCE_DIR="$CWD/files/ftest"
FTEST_INSTALL_DIR="$DAOS_INSTALL/lib/daos/TESTING/ftest"
DAOS_LAUNCH_SH="$DAOS_WORKSPACE/daos-launch.sh"

# Live-install check: on every node involved, the activation symlink and the
# ExecStart of the installed daos_server/daos_agent units must resolve into
# this ticket's $DAOS_INSTALL. Paths are canonicalized on the node, where
# /scratch is a symlink (readlink -m: compare where they point, whether or not
# the binaries exist yet). Prints one line per mismatch and exits 1 on any.
# shellcheck disable=SC2016  # expanded remotely on purpose
LIVE_CHECK_SCRIPT='
set -u
expected="$(readlink -m "$1")"
rc=0
live="$(readlink -f /etc/ld.so.conf.d/daos-x86_64.conf 2>/dev/null || true)"
case "$live" in
	"$expected"/*) ;;
	*) echo "/etc/ld.so.conf.d/daos-x86_64.conf -> ${live:-(none)}" ; rc=1 ;;
esac
for unit in daos_server daos_agent ; do
	exec_start="$(systemctl cat "$unit.service" 2>/dev/null | sed -n "s/^ExecStart=//p" | tail -n 1)"
	[[ -n "$exec_start" ]] || continue
	bin="$(readlink -m "${exec_start%% *}")"
	case "$bin" in
		"$expected"/*) ;;
		*) echo "$unit.service ExecStart -> ${exec_start%% *}" ; rc=1 ;;
	esac
done
exit $rc
'
check_live_install() {
	local nodes rc=0 report
	nodes="$(nodeset -f "$LOGIN_NODE" "$SERVERS" ${CLIENTS:+"$CLIENTS"})"
	echo ">>> Checking that $DAOS_INSTALL is the live install on $nodes"
	set +e
	report="$(clush -b -S -w "$nodes" bash -c "${LIVE_CHECK_SCRIPT@Q} live-check ${DAOS_INSTALL@Q}" 2>&1)"
	rc=$?
	set -e
	if [[ $rc -eq 0 ]] ; then
		echo ">>> Live install check: OK"
		return 0
	fi
	echo "$report" >&2
	echo ">>> [ERROR] $DAOS_INSTALL is not the live install (see above): run build-daos.sh --activate from this ticket directory first, or pass --skip-live-check to force" >&2
	return 1
}

if $LIVE_CHECK ; then
	if $DRY_RUN ; then
		check_live_install || echo ">>> (dry-run) the live install check would refuse to run" >&2
	else
		check_live_install || exit 1
	fi
else
	echo ">>> [WARN] live install check skipped" >&2
fi

if $DRY_RUN ; then
	echo ">>> (dry-run) log: $LOG"
	if $OVERLAY && [[ -d "$FTEST_SOURCE_DIR" ]] ; then
		echo ">>> (dry-run) would install $FTEST_SOURCE_DIR/ onto $FTEST_INSTALL_DIR/ ($(find "$FTEST_SOURCE_DIR" \( -type f -o -type l \) | wc -l) files)"
	else
		echo ">>> (dry-run) no ftest overlay"
	fi
	echo ">>> (dry-run) would run on $LOGIN_NODE:"
	echo "cd ${DAOS_WORKSPACE@Q} && ./daos-launch.sh -sv -- ${LAUNCH[*]@Q}"
	exit 0
fi

if $OVERLAY && [[ -d "$FTEST_SOURCE_DIR" ]] ; then
	echo ">>> Installing $FTEST_SOURCE_DIR on top of $FTEST_INSTALL_DIR"
	if [[ -d "$FTEST_INSTALL_DIR" ]] ; then
		# Install tree visible from here (shared /scratch): copy only what changed.
		while IFS= read -r file_path ; do
			if ! cmp -s "$FTEST_SOURCE_DIR/$file_path" "$FTEST_INSTALL_DIR/$file_path" ; then
				mkdir -p "$(dirname "$FTEST_INSTALL_DIR/$file_path")"
				cp -fv "$FTEST_SOURCE_DIR/$file_path" "$FTEST_INSTALL_DIR/$file_path"
			fi
		done < <(find "$FTEST_SOURCE_DIR" \( -type f -o -type l \) -printf '%P\n')
	else
		ssh "$LOGIN_NODE" test -d "$FTEST_INSTALL_DIR" || die "$FTEST_INSTALL_DIR not found on $LOGIN_NODE: run build-daos.sh first"
		scp -rq "$FTEST_SOURCE_DIR/." "$LOGIN_NODE:$FTEST_INSTALL_DIR/"
	fi
fi

echo ">>> Running functional tests on $LOGIN_NODE: ${TESTS[*]}"
echo ">>> Servers: $SERVERS; clients: ${CLIENTS:-none}; nvme: ${NVME:-none (ram only)}; scm_size: ${SCM_SIZE:-default}; provider: $PROVIDER"
echo ">>> Log: $LOG"
true > "$LOG"
set +e
{
	cat <<-EOF
	# set -x
	set -u -o pipefail

	SSH_AGENT_PID=\${SSH_AGENT_PID:-}

	if [[ ! -x "$DAOS_LAUNCH_SH" ]] ; then
		echo ">>> [ERROR] $DAOS_LAUNCH_SH not found or not executable." >&2
		echo "  Run provision-daos.sh first (from the ticket directory) to generate it for this ticket." >&2
		exit 1
	fi

	cd "$DAOS_WORKSPACE"
	./daos-launch.sh -sv -- ${LAUNCH[@]@Q}
	EOF
} | ssh "$LOGIN_NODE" bash -l -s |& tee -a "$LOG"
rc=${PIPESTATUS[1]}
set -e

echo
if [[ $rc -eq 0 ]] ; then
	echo ">>> Functional tests passed (log: $LOG)"
else
	echo ">>> [ERROR] Functional tests failed with exit code $rc (log: $LOG)" >&2
fi
exit "$rc"
