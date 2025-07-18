#!/bin/bash
#
#  Copyright 2022-2023 Intel Corporation.
#  Copyright 2025 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -eux

: "${DAOS_STACK_RETRY_DELAY_SECONDS:=60}"
: "${DAOS_STACK_RETRY_COUNT:=3}"
: "${DAOS_STACK_MONITOR_SECONDS:=600}"
: "${BUILD_URL:=Not_in_jenkins}"
: "${STAGE_NAME:=Unknown_Stage}"
: "${OPERATIONS_EMAIL:=$USER@localhost}"
: "${JENKINS_URL:=https://jenkins.example.com}"
domain1="${JENKINS_URL#https://}"
mail_domain="${domain1%%/*}"
: "${EMAIL_DOMAIN:=$mail_domain}"
: "${DAOS_DEVOPS_EMAIL:="$HOSTNAME"@"$EMAIL_DOMAIN"}"

# functions common to more than one distro specific provisioning
url_to_repo() {
    local url="$1"

    local repo=${url#*://}
    repo="${repo#/}"
    repo="${repo//%/}"
    repo="${repo//\//_}"

    echo "$repo"
}

add_repo() {
    local match="$1"
    local add_repo="$2"
    local gpg_check="${3:-true}"

    if [ -z "$match" ]; then
        # we cannot try to add a repo that has no match
        return
    fi

    local repo
    # see if a package we know is in the repo is present
    if repo=$(dnf -y repoquery --qf "%{repoid}" "$1" 2>/dev/null | grep ..\*); then
        DNF_REPO_ARGS+=" --enablerepo=$repo"
    else
        local repo_url="${REPOSITORY_URL}${add_repo}"
        local repo_name
        repo_name=$(url_to_repo "$repo_url")
        if ! dnf -y repolist | grep "$repo_name"; then
            dnf -y config-manager --add-repo="${repo_url}" >&2
            if ! $gpg_check; then
                disable_gpg_check "$add_repo" >&2
            fi
        fi
        DNF_REPO_ARGS+=" --enablerepo=$repo_name"
    fi
}

disable_gpg_check() {
    local url="$1"

    repo="$(url_to_repo "$url")"
    # bug in EL7 DNF: this needs to be enabled before it can be disabled
    dnf -y config-manager --save --setopt="$repo".gpgcheck=1
    dnf -y config-manager --save --setopt="$repo".gpgcheck=0
    # but even that seems to be not enough, so just brute-force it
    if [ -d /etc/yum.repos.d ] &&
       ! grep gpgcheck /etc/yum.repos.d/"$repo".repo; then
        echo "gpgcheck=0" >> /etc/yum.repos.d/"$repo".repo
    fi
}

dump_repos() {
    for file in "$REPOS_DIR"/*.repo; do
        echo "---- $file ----"
        cat "$file"
    done
}
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
                # shellcheck disable=SC2154
                send_mail "Command retry successful in $STAGE_NAME after $((attempt + 1)) attempts using ${repo_servers[0]} as initial repo server " \
                          "Command:  ${args[*]}\nAttempts: $attempt\nStatus:   $rc"
            fi
            if [ -n "$ARTIFACTORY_URL" ]; then
                dnfx="dnf"
                if command -v dnf4; then
                    dnfx="dnf4"
                fi
                "$dnfx" config-manager --disable 'epel*' || true
            fi
            return 0
        fi
        # Command failed, retry
        rc=${PIPESTATUS[0]}
        (( attempt++ )) || true
        if [ "$attempt" -gt 0 ]; then
            # shellcheck disable=SC2154
            if [ "$attempt" -eq 2 ] && [ ${#repo_servers[@]} -gt 1 ]; then
                # but we were using an experimental repo server, so fall back to the
                # non-experimental one after trying twice with the experimental one
                set_local_repo "${repo_servers[1]}"
                dnf -y makecache
            fi
            sleep "${RETRY_DELAY_SECONDS:-$DAOS_STACK_RETRY_DELAY_SECONDS}"
        fi
    done
    if [ "$rc" -ne 0 ]; then
        send_mail "Command retry failed in $STAGE_NAME after $attempt attempts using ${repo_server:-nexus} as initial repo server " \
                  "Command:  $*\nAttempts: $attempt\nStatus:   $rc"
        echo "Command retry failed in $STAGE_NAME after $attempt attempts using ${repo_server:-nexus} as initial repo server "
        echo "Command:  $*"
        echo "Attempts: $attempt"
        echo "Status:   $rc"
    fi
    return 1

}

send_mail() {
    local subject="$1"
    local message="${2:-}"

    local recipients
    if mail --help | grep s-nail; then
        recipients="${OPERATIONS_EMAIL//, }"
    else
        recipients="${OPERATIONS_EMAIL}"
    fi

    set +x
    {
        echo "Build: $BUILD_URL"
        echo "Stage: $STAGE_NAME"
        echo "Host:  $HOSTNAME"
        echo ""
        echo -e "$message"
    } 2>&1 | mail -s "$subject" -r "$DAOS_DEVOPS_EMAIL" "$recipients"
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
    ((duration = SECONDS - start))
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
        echo "Command retry failed in $STAGE_NAME after $attempt attempts"
        echo "Command:  $*"
        echo "Attempts: $attempt"
        echo "Status:   $rc"
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
        echo "Command timeout failed in $STAGE_NAME after $attempt attempts"
        echo "Command:  $*"
        echo "Attempts: $attempt"
        echo "Status:   $rc"
    fi
    return "$rc"
}

pr_repos() {
    if [ -n "$CI_PR_REPOS" ]; then
        echo "$CI_PR_REPOS"
        return 0
    fi

    echo "$COMMIT_MESSAGE" |
             sed -ne '/^PR-repos: */s/^[^:]*: *//Ip' \
                  -e "/^PR-repos-$DISTRO: */s/^[^:]*: *//Ip" | tr '\n' ' '
    return 0
}

rpm_test_version() {
    if [ -n "$CI_RPM_TEST_VERSION" ]; then
        echo "$CI_RPM_TEST_VERSION"
        return 0
    fi

    echo "$COMMIT_MESSAGE" |
             sed -ne '/^RPM-test-version: */s/^[^:]*: *//Ip'
    return 0

}

set_local_repo() {
    local repo_server="$1"

    # shellcheck disable=SC1091
    . /etc/os-release

    if [ "$repo_server" = "artifactory" ]; then
        if { [[ \ $(pr_repos) = *\ daos@PR-* ]] || [ -z "$(rpm_test_version)" ]; } &&
           [[ ! ${CHANGE_TARGET:-$BRANCH_NAME} =~ ^[-.0-9A-Za-z]+-testing ]]; then
            # Disable the daos repo so that the Jenkins job repo or a PR-repos*: repo is
            # used for daos packages
            dnf -y config-manager \
                --disable daos-stack-daos-"${DISTRO_GENERIC}"-"${VERSION_ID%%.*}"*-stable-local-artifactory
        else
            dnf -y config-manager \
                --enable daos-stack-daos-"${DISTRO_GENERIC}"-"${VERSION_ID%%.*}"*-stable-local-artifactory
        fi
        dnf -y config-manager \
            --enable daos-stack-deps-"${DISTRO_GENERIC}"-"${VERSION_ID%%.*}"*-stable-local-artifactory
    fi

    dnf repolist
}

update_repos() {
    local DISTRO_NAME="$1"

    # This is not working right on a second run.
    # using a quick hack to stop deleting a critical repo
    local file
    for file in "$REPOS_DIR"/*.repo; do
        [[ $file == *"artifactory"* ]] && continue
        [ -e "$file" ] || break
        # empty the file but keep it around so that updates don't recreate it
        true > "$file"
    done

    set_local_repo "${repo_servers[0]}"
}

post_provision_config_nodes() {
    # shellcheck disable=SC2154
    if ! update_repos "$DISTRO_NAME"; then
        # need to use the image supplied repos
        # shellcheck disable=SC2034
        repo_servers=()
    fi

    bootstrap_dnf

    # Reserve port ranges 31416-31516 for DAOS and CART servers
    echo 31416-31516 > /proc/sys/net/ipv4/ip_local_reserved_ports

    # Remove DAOS dependencies to prevent masking packaging bugs
    if rpm -qa | grep fuse3; then
        dnf -y erase fuse3\*
    fi

    if [ -n "$CONFIG_POWER_ONLY" ]; then
        rm -f "$REPOS_DIR"/*_job_daos-stack_job_*_job_*.repo
        time dnf -y erase fio fuse ior-hpc mpich-autoload          \
                     argobots cart daos daos-client dpdk      \
                     libisa-l libpmemobj mercury mpich   \
                     pmix protobuf-c spdk libfabric libpmem        \
                     munge-libs munge slurm                        \
                     slurm-example-configs slurmctld slurm-slurmmd
    fi

    cat /etc/os-release

    # ConnectX must be 5 or later to support MOFED/DOCA drivers
    # RoCE tests with Mellanox adapters may use MOFED/DOCA drivers.
    last_pci_bus=''
    mellanox_drivers=false
    while IFS= read -r line; do
        pci_bus="${line%.*}"
        if [ "$pci_bus" == "$last_pci_bus" ]; then
            # We only use one interface on a dual interface HBA
            # Fortunately lspci appears to group them together
            continue
        fi
        last_pci_bus="$pci_bus"
        mlnx_type="${line##*ConnectX-}"
        mlnx_type="${mlnx_type%]*}"
        if [ "$mlnx_type" -ge 5 ]; then
            mellanox_drivers=true
            break
        fi
    done < <(lspci -mm | grep "ConnectX")

    if "$mellanox_drivers"; then
        # Remove OPA and install MOFED
        install_mofed
    fi

    local repos_added=()
    local repo
    local inst_repos=()
    # shellcheck disable=SC2153
    read -ra inst_repos <<< "$INST_REPOS"
    for repo in "${inst_repos[@]}"; do
        branch="master"
        build_number="lastSuccessfulBuild"
        if [[ $repo = *@* ]]; then
            branch="${repo#*@}"
            repo="${repo%@*}"
            if [[ \ ${repos_added[*]+${repos_added[*]}}\  = *\ ${repo}\ * ]]; then
                # don't add duplicates, first found wins
                continue
            fi
            repos_added+=("$repo")
            if [[ $branch = *:* ]]; then
                build_number="${branch#*:}"
                branch="${branch%:*}"
            fi
        fi
        local subdir
        if ! $COVFN_DISABLED; then
            subdir="bullseye/"
        fi
        local repo_url="${ARTIFACTS_URL:-${JENKINS_URL}job/}"daos-stack/job/"$repo"/job/"${branch//\//%252F}"/"$build_number"/artifact/artifacts/"${subdir:-}"$DISTRO_NAME/
        dnf -y config-manager --add-repo="$repo_url"
        repo="$(url_to_repo "$repo_url")"
        # PR-repos: should always be able to upgrade modular packages
        dnf -y config-manager --save --setopt "$repo.module_hotfixes=true" "$repo"
        disable_gpg_check "$repo_url"
    done

    # start with everything fully up-to-date
    # all subsequent package installs beyond this will install the newest packages
    # but we need some hacks for images with MOFED already installed
    cmd=(retry_dnf 600)
    if grep MOFED_VERSION /etc/do-release; then
        cmd+=(--setopt=best=0 upgrade --exclude "$EXCLUDE_UPGRADE")
    else
        cmd+=(upgrade)
    fi
    if ! "${cmd[@]}"; then
        dump_repos
        echo "Failed to upgrade packages"
        return 1
    fi

    local inst_rpms=()
    # shellcheck disable=SC2153
    if [ -n "$INST_RPMS" ]; then
        # use eval here, rather than say, read -ra to take advantage of bash globbing
        eval "inst_rpms=($INST_RPMS)"
        time dnf -y erase "${inst_rpms[@]}" libfabric
    fi
    rm -f /etc/profile.d/openmpi.sh
    rm -f /tmp/daos_control.log

    # shellcheck disable=SC2001
    if [ ${#inst_rpms[@]} -gt 0 ]; then
        if ! retry_dnf 360 install "${inst_rpms[@]/%/${DAOS_VERSION:-}}"; then
            rc=${PIPESTATUS[0]}
            dump_repos
            echo "Failed to install packages"
            return "$rc"
        fi
    fi

    if lspci | grep "ConnectX-6" && ! grep MOFED_VERSION /etc/do-release; then
        printf 'MOFED_VERSION=%s\n' "$MLNX_VER_NUM" >> /etc/do-release
    fi

    distro_custom

    cat /etc/os-release

    if [ -f /etc/do-release ]; then
        cat /etc/do-release
    fi

    return 0
}
