#!/bin/bash

REPOS_DIR=/etc/yum.repos.d
DISTRO_NAME=centos8
LSB_RELEASE=redhat-lsb-core
EXCLUDE_UPGRADE=dpdk,fuse,mercury,daos,daos-\*
POWERTOOLSREPO="powertools"

bootstrap_dnf() {
    # hack in the removal of group repos
    version="$(lsb_release -sr)"
    version=${version%.*}
    if dnf repolist | grep "repo.dc.hpdd.intel.com_repository_centos-${version}-x86_64-group_"; then
        rm -f /etc/yum.repos.d/repo.dc.hpdd.intel.com_repository_{centos-8.4,daos-stack-centos-8}-x86_64-group_.repo
        for repo in centos-${version}-{base,extras,powertools} epel-el-8; do
            my_repo="${REPOSITORY_URL}repository/$repo-x86_64-proxy"
            my_name="${my_repo#*//}"
            my_name="${my_name//\//_}"
            echo -e "[${my_name}]
name=created from ${my_repo}
baseurl=${my_repo}
enabled=1
repo_gpgcheck=0
gpgcheck=1" >> /etc/yum.repos.d/local-centos-"$repo".repo
        done
        my_repo="${REPOSITORY_URL}/repository/daos-stack-el-8-x86_64-stable-local"
        my_name="${my_repo#*//}"
        my_name="${my_name//\//_}"
        echo -e "[${my_name}]
name=created from ${my_repo}
baseurl=${my_repo}
enabled=1
repo_gpgcheck=0
gpgcheck=0" >> /etc/yum.repos.d/local-daos-group.repo
    fi

    systemctl enable postfix.service
    systemctl start postfix.service
}

group_repo_post() {
    # Nothing to do for EL
    :
}

distro_custom() {
    # install avocado
    dnf -y install python3-avocado{,-plugins-{output-html,varianter-yaml-to-mux}} \
                   clustershell

    # why do we disable this?
    dnf -y config-manager --disable "$POWERTOOLSREPO"

}

post_provision_config_nodes() {
    bootstrap_dnf

    # Reserve port ranges 31416-31516 for DAOS and CART servers
    echo 31416-31516 > /proc/sys/net/ipv4/ip_local_reserved_ports

    if $CONFIG_POWER_ONLY; then
        rm -f $REPOS_DIR/*.hpdd.intel.com_job_daos-stack_job_*_job_*.repo
        time dnf -y erase fio fuse ior-hpc mpich-autoload               \
                     ompi argobots cart daos daos-client dpdk      \
                     fuse-libs libisa-l libpmemobj mercury mpich   \
                     openpa pmix protobuf-c spdk libfabric libpmem \
                     libpmemblk munge-libs munge slurm             \
                     slurm-example-configs slurmctld slurm-slurmmd
    fi

    time dnf -y repolist
    # the group repo is always on the test image
    #add_group_repo
    #add_local_repo

    # CORCI-1096
    # workaround until new snapshot images are produced
    # Assume if APPSTREAM is locally proxied so is epel-modular
    # so disable the upstream epel-modular repo
    if [ -n "${DAOS_STACK_EL_8_APPSTREAM_REPO:-}" ]; then
        dnf -y config-manager --disable epel-modular appstream powertools
    fi

    # Use remote repo config instead of image-installed repos
    # shellcheck disable=SC2207
    if ! old_repo_files=($(ls "${REPOS_DIR}"/*.repo)); then
        echo "Failed to determine old repo files"
        exit 1
    fi
    local repo_server
    repo_server=$(echo "$COMMIT_MESSAGE" | sed -ne '/^Repo-server: */s/.*: *//p')
    if [ "$repo_server" = "nexus" ]; then
        repo_server=""
    elif [ "$repo_server" = "" ]; then
        repo_server="artifactory"
    fi
    if ! fetch_repo_config "$repo_server"; then
        # leave the existing on-image repo config alone if the repo fetch fails
        send_mail "Fetch repo file for repo server \"$repo_server\" failed.  Continuing on with in-image repos."
    else
        if ! rm -f "${old_repo_files[@]}"; then
            echo "Failed to remove old repo files"
            exit 1
        fi
        if [ "$DISTRO_NAME" = "centos8" ]; then
            # shellcheck disable=SC2034
            POWERTOOLSREPO="daos_ci-centos8-powertools"
        fi
    fi
    time dnf -y repolist

    if [ -n "$INST_REPOS" ]; then
        local repo
        for repo in $INST_REPOS; do
            branch="master"
            build_number="lastSuccessfulBuild"
            if [[ $repo = *@* ]]; then
                branch="${repo#*@}"
                repo="${repo%@*}"
                if [[ $branch = *:* ]]; then
                    build_number="${branch#*:}"
                    branch="${branch%:*}"
                fi
            fi
            local repo_url="${JENKINS_URL}"job/daos-stack/job/"${repo}"/job/"${branch//\//%252F}"/"${build_number}"/artifact/artifacts/$DISTRO_NAME/
            dnf -y config-manager --add-repo="${repo_url}"
            disable_gpg_check "$repo_url"
        done
    fi
    if [ -n "$INST_RPMS" ]; then
        # shellcheck disable=SC2086
        time dnf -y erase $INST_RPMS
    fi
    rm -f /etc/profile.d/openmpi.sh
    rm -f /tmp/daos_control.log
    if [ -n "${LSB_RELEASE:-}" ]; then
        RETRY_COUNT=4 retry_dnf 360 "${repo_server}" install $LSB_RELEASE
    fi

    if [ "$DISTRO_NAME" = "centos7" ] && lspci | grep "ConnectX-6"; then
        # No openmpi3 or MACSio-openmpi3 can be installed currently
        # when the ConnnectX-6 driver is installed
        INST_RPMS="${INST_RPMS// openmpi3/}"
        INST_RPMS="${INST_RPMS// MACSio-openmpi3}"
    fi

    # shellcheck disable=SC2086
    if [ -n "$INST_RPMS" ]; then
        if ! RETRY_COUNT=4 retry_dnf 360 "${repo_server}" install $INST_RPMS; then
            rc=${PIPESTATUS[0]}
            dump_repos
            exit "$rc"
        fi
    fi

    distro_custom

    lsb_release -a

    # now make sure everything is fully up-to-date
    if ! RETRY_COUNT=4 retry_dnf 600 "${repo_server}" upgrade --exclude "$EXCLUDE_UPGRADE"; then
        dump_repos
        exit 1
    fi

    lsb_release -a

    if [ -f /etc/do-release ]; then
        cat /etc/do-release
    fi
    cat /etc/os-release

    exit 0
}
