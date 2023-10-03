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
                # shellcheck disable=SC2154
                send_mail "Command retry successful in $STAGE_NAME after $((attempt + 1)) attempts using ${repo_servers[0]} as initial repo server " \
                          "Command:  ${args[*]}\nAttempts: $attempt\nStatus:   $rc"
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
                if [ -n "${POWERTOOLSREPO:-}" ]; then
                    POWERTOOLSREPO=${POWERTOOLSREPO/${repo_servers[0]}/${repo_servers[1]}}
                fi
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
    } 2>&1 | mail -s "$subject" -r "$HOSTNAME"@intel.com "$recipients"
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

    . /etc/os-release
    local repo_file="daos_ci-${ID}${VERSION_ID%%.*}-$repo_server"
    local repopath="${REPOS_DIR}/$repo_file"
    if ! curl -f -o "$repopath" "$REPO_FILE_URL$repo_file.repo"; then
        return 1
    fi

    return 0
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

    . /etc/os-release

    rm -f "$REPOS_DIR/daos_ci-${ID}${VERSION_ID%%.*}".repo
    ln "$REPOS_DIR/daos_ci-${ID}${VERSION_ID%%.*}"{-"$repo_server",.repo}

    if [ "$repo_server" = "artifactory" ]; then
        if { [[ \ $(pr_repos) = *\ daos@PR-* ]] || [ -z "$(rpm_test_version)" ]; } &&
           [[ ! ${CHANGE_TARGET:-$BRANCH_NAME} =~ ^[-.0-9A-Za-z]+-testing ]]; then
            # Disable the daos repo so that the Jenkins job repo or a PR-repos*: repo is
            # used for daos packages
            dnf -y config-manager \
                --disable daos-stack-daos-"${DISTRO_GENERIC}"-"${VERSION_ID%%.*}"-x86_64-stable-local-artifactory
        fi
        # Disable module filtering for our deps repo
        deps_repo="daos-stack-deps-${DISTRO_GENERIC}-${VERSION_ID%%.*}-x86_64-stable-local-artifactory"
        dnf config-manager --save --setopt "$deps_repo.module_hotfixes=true" "$deps_repo"
    fi

    dnf repolist
}

update_repos() {
    local DISTRO_NAME="$1"

    # Update the repo files
    local repo_server
    for repo_server in "${repo_servers[@]}"; do
        if ! fetch_repo_config "$repo_server"; then
            # leave the existing on-image repo config alone if the repo fetch fails
            send_mail "Fetch repo file for repo server \"$repo_server\" failed.  Continuing on with in-image repos."
            return 1
        fi
    done

    # we're not actually using the set_local_repos.sh script
    # setting a repo server is as easy as renaming a file
    #if ! curl -o /usr/local/sbin/set_local_repos.sh-tmp "${REPO_FILE_URL}set_local_repos.sh"; then
    #    send_mail "Fetch set_local_repos.sh failed.  Continuing on with in-image copy."
    #else
    #    cat /usr/local/sbin/set_local_repos.sh-tmp > /usr/local/sbin/set_local_repos.sh
    #    chmod +x /usr/local/sbin/set_local_repos.sh
    #    rm -f /usr/local/sbin/set_local_repos.sh-tmp
    #fi

    # successfully grabbed them all, so replace the entire $REPOS_DIR
    # content with them
    local file
    for file in "$REPOS_DIR"/*.repo; do
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

    if $CONFIG_POWER_ONLY; then
        rm -f "$REPOS_DIR"/*.hpdd.intel.com_job_daos-stack_job_*_job_*.repo
        time dnf -y erase fio fuse ior-hpc mpich-autoload               \
                     ompi argobots cart daos daos-client dpdk      \
                     fuse-libs libisa-l libpmemobj mercury mpich   \
                     pmix protobuf-c spdk libfabric libpmem        \
                     libpmemblk munge-libs munge slurm             \
                     slurm-example-configs slurmctld slurm-slurmmd
    fi

    cat /etc/os-release

    # start with everything fully up-to-date
    # all subsequent package installs beyond this will install the newest packages
    # but we need some hacks for images with MOFED already installed
    cmd=(retry_dnf 600)
    if grep MOFED_VERSION /etc/do-release; then
        cmd+=(--setopt=best=0 upgrade --exclude "$EXCLUDE_UPGRADE")
    else
        cmd+=(upgrade)
    fi
    if ! "${cmd[@]}" --exclude golang-*.daos.*; then
        dump_repos
        return 1
    fi

    if lspci | grep "ConnectX-6" && ! grep MOFED_VERSION /etc/do-release; then
        # Remove OPA and install MOFED
        install_mofed
    fi

    local repos_added=()
    local repo
    local inst_repos=()
    # shellcheck disable=SC2153
    read -ra inst_repos <<< "$INST_REPOS"
    for repo in "${inst_repos[@]+"${inst_repos[@]}"}"; do
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
        local repo_url="${JENKINS_URL}"job/daos-stack/job/"${repo}"/job/"${branch//\//%252F}"/"${build_number}"/artifact/artifacts/$DISTRO_NAME/
        dnf -y config-manager --add-repo="${repo_url}"
        disable_gpg_check "$repo_url"
    done
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
        if ! retry_dnf 360 install "${inst_rpms[@]}"; then
            rc=${PIPESTATUS[0]}
            dump_repos
            return "$rc"
        fi
    fi

    if lspci | grep "ConnectX-6" && ! grep MOFED_VERSION /etc/do-release; then
        # Need this module file
        version="$(rpm -q --qf "%{version}" openmpi)"
        mkdir -p /etc/modulefiles/mpi/
        cat << EOF > /etc/modulefiles/mpi/mlnx_openmpi-x86_64
#%Module 1.0
#
#  OpenMPI module for use with 'environment-modules' package:
#
conflict		mpi
prepend-path 		PATH 		/usr/mpi/gcc/openmpi-$version/bin
prepend-path 		LD_LIBRARY_PATH /usr/mpi/gcc/openmpi-$version/lib64
prepend-path 		PKG_CONFIG_PATH	/usr/mpi/gcc/openmpi-$version/lib64/pkgconfig
prepend-path		MANPATH		/usr/mpi/gcc/openmpi-$version/share/man
setenv 			MPI_BIN		/usr/mpi/gcc/openmpi-$version/bin
setenv			MPI_SYSCONFIG	/usr/mpi/gcc/openmpi-$version/etc
setenv			MPI_FORTRAN_MOD_DIR	/usr/mpi/gcc/openmpi-$version/lib64
setenv			MPI_INCLUDE	/usr/mpi/gcc/openmpi-$version/include
setenv	 		MPI_LIB		/usr/mpi/gcc/openmpi-$version/lib64
setenv			MPI_MAN			/usr/mpi/gcc/openmpi-$version/share/man
setenv			MPI_COMPILER	openmpi-x86_64
setenv			MPI_SUFFIX	_openmpi
setenv	 		MPI_HOME	/usr/mpi/gcc/openmpi-$version
EOF

        printf 'MOFED_VERSION=%s\n' "$MLNX_VER_NUM" >> /etc/do-release
    fi 

    distro_custom

    cat /etc/os-release

    if [ -f /etc/do-release ]; then
        cat /etc/do-release
    fi

    return 0
}
