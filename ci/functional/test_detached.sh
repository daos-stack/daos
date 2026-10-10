#!/bin/bash
#
#  Copyright 2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Run ci/functional/test_main_node.sh on the first test node detached from the
# CI agent, so that restarting the Jenkins controller or agent does not kill the
# tests.  The agent only needs short-lived ssh connections to check progress.
#
# Usage:
#   test_detached.sh launch  Start the tests on FIRST_NODE (called by test_main.sh)
#   test_detached.sh wait [INTERVAL [DEADLINE]]
#                            Stream console output until the run finishes,
#                            checking every INTERVAL seconds (default 30).
#                            DEADLINE is an optional epoch time to give up at.
#   test_detached.sh poll    Print new console output and report progress once
#   test_detached.sh kill    Stop a running detached test run
#
# wait and poll exit status:
#   0  the run has finished; its exit status is in ftest_detached/rc
#   3  the run is still in progress (poll only)
#   4  FIRST_NODE could not be reached; retry later
#   5  no detached run was launched (e.g. the tests ran inline)
#   6  DEADLINE passed before the run finished (wait only)
#
set -euo pipefail

state_dir="ftest_detached"
state_file="$state_dir/state"
# Saved in the state file by launch and restored by load_state
RUN_DIR=""
CONSOLE_LOG=""
ssh_opts=(-i ci_key -l jenkins -o BatchMode=yes -o ConnectTimeout=30
          -o ServerAliveInterval=15 -o ServerAliveCountMax=4)

remote() {
    # shellcheck disable=SC2029
    ssh "${ssh_opts[@]}" "$FIRST_NODE" "$@"
}

load_state() {
    [[ -f "$state_file" ]] || return 1
    # shellcheck disable=SC1090
    source "$state_file"
}

unmount_share() {
    clush -B -S -o '-i ci_key' -l root -w "$TNODES" "set -x; umount /mnt/share" || true
}

launch() {
    : "${FIRST_NODE:?FIRST_NODE must be set}"
    : "${TNODES:?TNODES must be set}"
    : "${TEST_TAG:?TEST_TAG must be set}"
    : "${STAGE_NAME:?STAGE_NAME must be set}"

    local run_id
    run_id=$(printf '%s-%s' "${BUILD_TAG:-manual}" "$STAGE_NAME" | tr -c 'A-Za-z0-9._-' '_')
    RUN_DIR="/var/tmp/ftest-run/$run_id"
    CONSOLE_LOG="$STAGE_NAME/ftest_console.log"

    mkdir -p "$state_dir" "$STAGE_NAME"
    rm -f "$state_dir/rc" "$state_dir/offset"
    : > "$CONSOLE_LOG"
    printf '%s=%q\n' FIRST_NODE "$FIRST_NODE" TNODES "$TNODES" \
        RUN_DIR "$RUN_DIR" CONSOLE_LOG "$CONSOLE_LOG" > "$state_file"

    if remote "test -e $RUN_DIR/pid"; then
        echo "Tests already launched in $FIRST_NODE:$RUN_DIR; reattaching"
        return 0
    fi

    {
        printf 'export %s=%q\n' \
            TEST_TAG "$TEST_TAG" \
            TNODES "$TNODES" \
            FTEST_ARG "${FTEST_ARG:-}" \
            WITH_VALGRIND "${WITH_VALGRIND:-}" \
            STAGE_NAME "$STAGE_NAME" \
            DAOS_HTTPS_PROXY "${DAOS_HTTPS_PROXY:-}" \
            DAOS_NO_PROXY "${DAOS_NO_PROXY:-}"
        cat ci/functional/test_main_node.sh
    } | remote "rm -rf $RUN_DIR && mkdir -p $RUN_DIR && cat > $RUN_DIR/run.sh"

    # The wrapper runs as a session leader, so its pid is also the process
    # group id that kill uses.  rc is written atomically so that poll never
    # reads a partial value.
    remote "cat > $RUN_DIR/wrapper.sh" <<'EOF'
#!/bin/bash
cd "$(dirname "$0")"
echo $$ > pid
bash run.sh > console.log 2>&1 < /dev/null
echo $? > rc.tmp
mv rc.tmp rc
EOF

    # setsid puts the run in its own session so it outlives this ssh
    # connection.  Wait for the wrapper to record its pid before returning.
    remote "cd $RUN_DIR || exit 1
            setsid nohup bash wrapper.sh > /dev/null 2>&1 < /dev/null &
            for _ in \$(seq 20); do [ -s pid ] && exit 0; sleep 0.5; done
            echo 'Detached test run did not start' >&2; exit 1"
    echo "Launched tests on $FIRST_NODE in $RUN_DIR"
}

# Check the run once, print and save any new console output, and set STATUS to
# running, "done <rc>", died or unreachable.  INFO holds heartbeat details.
STATUS=""
INFO=""
check() {
    local offset=0 chunk result
    if [[ -f "$state_dir/offset" ]]; then
        offset=$(<"$state_dir/offset")
    fi

    # Check rc before and after the pid so a run that exits between the two
    # checks is not reported as having died.
    if ! result=$(remote "cd $RUN_DIR || exit 1
        now=\$(date +%s)
        log=\$(ls -t /var/tmp/ftest/avocado/job-results/job-*/job.log 2>/dev/null | head -1)
        info=\"started=\$((now - \$(stat -c %Y pid))) output=\$((now - \$(stat -c %Y console.log)))\"
        [ -n \"\$log\" ] && info=\"\$info avocado=\$((now - \$(stat -c %Y \$log)))\"
        if [ -f rc ]; then echo \"done \$(cat rc)\";
        elif kill -0 \$(cat pid) 2>/dev/null; then echo running;
        elif [ -f rc ]; then echo \"done \$(cat rc)\";
        else echo died; fi
        echo \"\$info\""); then
        STATUS=unreachable
        return
    fi
    STATUS=$(head -1 <<< "$result")
    INFO=$(tail -1 <<< "$result")

    # Fetch output after the status check so a finished run's log is complete.
    chunk=$(mktemp)
    if remote "tail -c +$((offset + 1)) $RUN_DIR/console.log 2>/dev/null" > "$chunk"; then
        cat "$chunk"
        cat "$chunk" >> "$CONSOLE_LOG"
        echo $((offset + $(wc -c < "$chunk"))) > "$state_dir/offset"
    fi
    rm -f "$chunk"
}

# Record the result of a finished run and clean up the test nodes.
finish() {
    if [[ "$STATUS" == done\ * ]]; then
        echo "${STATUS#done }" > "$state_dir/rc"
    else
        echo "Test process on $FIRST_NODE exited without writing a result"
        echo 255 > "$state_dir/rc"
    fi
    unmount_share
    exit 0
}

# Turn "started=N output=N avocado=N" (seconds) into a readable heartbeat.
heartbeat() {
    local started="" output="" avocado="" kv msg
    for kv in $INFO; do
        case "$kv" in
            started=*) started=${kv#*=} ;;
            output=*) output=${kv#*=} ;;
            avocado=*) avocado=${kv#*=} ;;
        esac
    done
    msg="Tests still running on $FIRST_NODE for $((${started:-0} / 60))m"
    msg+="; last console output $((${output:-0} / 60))m ago"
    if [[ -n "$avocado" ]]; then
        msg+="; avocado job.log updated ${avocado}s ago"
    fi
    echo "[$(date -u +%FT%TZ)] $msg"
}

poll() {
    if ! load_state; then
        echo "No detached test run was launched"
        exit 5
    fi
    check
    case "$STATUS" in
        unreachable)
            echo "Unable to reach $FIRST_NODE; will retry"
            exit 4
            ;;
        running)
            heartbeat
            exit 3
            ;;
    esac
    finish
}

wait_run() {
    local interval=${1:-30} deadline=${2:-0}
    local heartbeat_every=${FTEST_HEARTBEAT:-600} max_unreachable=${FTEST_MAX_UNREACHABLE:-20}
    local unreachable=0 last_beat offset before

    if ! load_state; then
        echo "No detached test run was launched"
        exit 5
    fi
    echo "Waiting for the detached test run on $FIRST_NODE:$RUN_DIR"
    last_beat=$(date +%s)
    while true; do
        before=0
        if [[ -f "$state_dir/offset" ]]; then
            before=$(<"$state_dir/offset")
        fi
        check
        case "$STATUS" in
            unreachable)
                unreachable=$((unreachable + 1))
                echo "Unable to reach $FIRST_NODE ($unreachable/$max_unreachable)"
                if ((unreachable >= max_unreachable)); then
                    exit 4
                fi
                ;;
            running)
                unreachable=0
                offset=$(cat "$state_dir/offset" 2>/dev/null || echo 0)
                if ((offset != before)); then
                    last_beat=$(date +%s)
                elif (($(date +%s) - last_beat >= heartbeat_every)); then
                    heartbeat
                    last_beat=$(date +%s)
                fi
                ;;
            *)
                finish
                ;;
        esac
        if ((deadline > 0 && $(date +%s) >= deadline)); then
            echo "Deadline passed before the detached test run finished"
            exit 6
        fi
        sleep "$interval"
    done
}

kill_run() {
    if ! load_state; then
        exit 0
    fi
    echo "Stopping detached test run on $FIRST_NODE:$RUN_DIR"
    # Besides the run's own session, stop the session that ftest.sh starts on
    # this node with "ssh ... bash -c FIRST_NODE=...".  The remote end of that
    # ssh is not part of the run's session and would otherwise keep running.
    # The first node is reserved for this stage, so any such session is ours.
    remote "bash -s $RUN_DIR" <<'EOF' || true
cd "$1" 2>/dev/null && [ -f pid ] && [ ! -f rc ] || exit 0
sessions="$(cat pid) $(ps -u "$(id -un)" -o pid=,sid=,args= |
    awk '$1 == $2 && $3 ~ /(^|\/)bash$/ && $4 == "-c" && $5 ~ /^FIRST_NODE=/ {print $1}')"
for sid in $sessions; do pkill -TERM -s "$sid"; done
for _ in $(seq 30); do
    alive=""
    for sid in $sessions; do pgrep -s "$sid" > /dev/null && alive=1; done
    [ -z "$alive" ] && exit 0
    sleep 1
done
for sid in $sessions; do pkill -KILL -s "$sid"; done
exit 0
EOF
    unmount_share
}

case "${1:-}" in
    launch) launch ;;
    wait) wait_run "${@:2}" ;;
    poll) poll ;;
    kill) kill_run ;;
    *)
        echo "Usage: $0 launch|wait [INTERVAL [DEADLINE]]|poll|kill" >&2
        exit 2
        ;;
esac
