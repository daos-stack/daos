#!/bin/bash

set -eux

: "${DAOS_STACK_RETRY_DELAY_SECONDS:=60}"
: "${DAOS_STACK_RETRY_COUNT:=3}"
: "${DAOS_STACK_MONITOR_SECONDS:=600}"
: "${BUILD_URL:=Not_in_jenkins}"
: "${STAGE_NAME:=Unknown_Stage}"
: "${OPERATIONS_EMAIL:=$USER@localhost}"

retry_dnf() {
    local monitor_threshold="$1"
    shift

    local args=("dnf" "-y" "${@}")
    local attempt=0
    local rc=0
    while [ $attempt -lt "${RETRY_COUNT:-$DAOS_STACK_RETRY_COUNT}" ]; do
        if monitor_cmd "$monitor_threshold" "${args[@]}"; then
            # Command succeeded, return with success
            if [ $attempt -gt 0 ]; then
                send_mail "Command retry successful in $STAGE_NAME after $attempt attempts using ${repo_server:-nexus} as initial repo server " \
                          "Command:  ${args[*]}\nAttempts: $attempt\nStatus:   $rc"
            fi
            return 0
        fi
        # Command failed, retry
        rc=${PIPESTATUS[0]}
        (( attempt++ )) || true
        if [ "$attempt" -gt 0 ]; then
            if [ "$attempt" -eq 2 ] && [ ${#repo_servers[@]} -gt 1 ]; then
                # but we were using an experimental repo server, so fall back to the
                # non-experimental one after trying twice with the experimental one
                set_local_repos.sh "${repo_servers[1]}"
                dnf -y makecache
            fi
            sleep "${RETRY_DELAY_SECONDS:-$DAOS_STACK_RETRY_DELAY_SECONDS}"
        fi
    done
    if [ "$rc" -ne 0 ]; then
        send_mail "Command retry failed in $STAGE_NAME after $attempt attempts using ${repo_server:-nexus} as initial repo server " \
                  "Command:  $*\nAttempts: $attempt\nStatus:   $rc"
    fi
    return 1

}

send_mail() {
    local subject="$1"
    local message="${2:-}"

    set +x
    {
        echo "Build: $BUILD_URL"
        echo "Stage: $STAGE_NAME"
        echo "Host:  $HOSTNAME"
        echo ""
        echo -e "$message"
    } 2>&1 | mail -s "$subject" -r "$HOSTNAME"@intel.com "$OPERATIONS_EMAIL"
    set -x
}

monitor_cmd() {
    local threshold="$1"
    shift

    local duration=0
    local start="$SECONDS"
    if ! time "$@"; then
        return "${PIPESTATUS[0]}"
    fi
    ((duration = SECONDS - start)) || true
    if [ "$duration" -gt "$threshold" ]; then
        send_mail "Command exceeded ${threshold}s in $STAGE_NAME" \
                    "Command:  $*\nReal time: $duration"
    fi
    return 0
}

retry_cmd() {
    local monitor_threshold="$1"
    shift

    local attempt=0
    local rc=0
    while [ $attempt -lt "${RETRY_COUNT:-$DAOS_STACK_RETRY_COUNT}" ]; do
        if monitor_cmd "$monitor_threshold" "$@"; then
            # Command succeeded, return with success
            if [ $attempt -gt 0 ]; then
                send_mail "Command retry successful in $STAGE_NAME after $attempt attempts" \
                          "Command:  $*\nAttempts: $attempt\nStatus:   $rc"
            fi
            return 0
        fi
        # Command failed, retry
        rc=${PIPESTATUS[0]}
        (( attempt++ )) || true
        if [ "$attempt" -gt 0 ]; then
            sleep "${RETRY_DELAY_SECONDS:-$DAOS_STACK_RETRY_DELAY_SECONDS}"
        fi
    done
    if [ "$rc" -ne 0 ]; then
        send_mail "Command retry failed in $STAGE_NAME after $attempt attempts" \
                  "Command:  $*\nAttempts: $attempt\nStatus:   $rc"
    fi
    return 1
}

timeout_cmd() {
    local timeout="$1"
    shift

    local attempt=0
    local rc=1
    while [ $attempt -lt "${RETRY_COUNT:-$DAOS_STACK_RETRY_COUNT}" ]; do
        if monitor_cmd "$DAOS_STACK_MONITOR_SECONDS" timeout "$timeout" "$@"; then
            # Command succeeded, return with success
            if [ $attempt -gt 0 ]; then
                send_mail "Command timeout successful in $STAGE_NAME after $attempt attempts" \
                          "Command:  $*\nAttempts: $attempt\nStatus:   $rc"
            fi
            return 0
        fi
        rc=${PIPESTATUS[0]}
        if [ "$rc" = "124" ]; then
            # Command timed out, try again
            (( attempt++ )) || true
            continue
        fi
        # Command failed for something other than timeout
        break
    done
    if [ "$rc" -ne 0 ]; then
        send_mail "Command timeout failed in $STAGE_NAME after $attempt attempts" \
                  "Command:  $*\nAttempts: $attempt\nStatus:   $rc"
    fi
    return "$rc"
}

fetch_repo_config() {
    local repo_server="$1"

    repo_file="daos_ci-${DISTRO_NAME}-$repo_server.repo"
    local repopath="${REPOS_DIR}/$repo_file"
    if ! curl -f -o "$repopath" \
              "$REPO_FILE_URL$repo_file"; then
        return 1
    fi
    # ugly hackery for nexus repo naming
    if [ "$repo_server" = "nexus" ]; then
        version="$(lsb_release -sr)"
        version=${version%.*}
        sed -i -e "s/\$releasever/$version/g" "$repopath"
    fi

    rpm_test_version="$(echo "$COMMIT_MESSAGE" | sed -ne '/^RPM-test-version: */s/.*: *//p')"
    if [ -z "$rpm_test_version" ] && [ "$repo_server" = "artifactory" ]; then
        # Disable the daos repo so that the Jenkins job repo is used for daos packages
        dnf -y config-manager --disable daos-stack-daos-el-8-x86_64-stable-local
    fi

    return 0
}

update_repos() {
    local DISTRO_NAME="$1"

    # Update the repo files
    local repo_server
    for repo_server in "${repo_servers[@]}"; do
        if ! fetch_repo_config "$repo_server"; then
            # leave the existing on-image repo config alone if the repo fetch fails
            send_mail "Fetch repo file for repo server \"$repo_server\" failed.  Continuing on with in-image repos."
        fi
    done
    time dnf -y repolist

    if ! curl -o /usr/local/sbin/set_local_repos.sh-tmp "${REPO_FILE_URL}set_local_repos.sh"; then
        send_mail "Fetch set_local_repos.sh failed.  Continuing on with in-image copy."
    else
        cat /usr/local/sbin/set_local_repos.sh-tmp > /usr/local/sbin/set_local_repos.sh
        chmod +x /usr/local/sbin/set_local_repos.sh
        rm -f /usr/local/sbin/set_local_repos.sh-tmp
    fi

    set_local_repos.sh "${repo_servers[0]}"
}
