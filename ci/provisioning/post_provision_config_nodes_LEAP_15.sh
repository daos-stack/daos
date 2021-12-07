#!/bin/bash

REPOS_DIR=/etc/dnf/repos.d
DISTRO_NAME=leap15
EXCLUDE_UPGRADE=fuse,fuse-libs,fuse-devel,mercury,daos,daos-\*

bootstrap_dnf() {
    rm -rf "$REPOS_DIR"
    ln -s ../zypp/repos.d "$REPOS_DIR"
}

group_repo_post() {
    if [ -n "$DAOS_STACK_GROUP_REPO" ]; then
        rpm --import \
            "${REPOSITORY_URL}${DAOS_STACK_GROUP_REPO%/*}/opensuse-15.2-devel-languages-go-x86_64-proxy/repodata/repomd.xml.key"
    fi
}

distro_custom() {
    # monkey-patch lua-lmod
    if ! grep MODULEPATH=".*"/usr/share/modules /etc/profile.d/lmod.sh; then \
        sed -e '/MODULEPATH=/s/$/:\/usr\/share\/modules/'                     \
               /etc/profile.d/lmod.sh;                                        \
    fi

    # force install of avocado 69.x
    dnf -y erase avocado{,-common}                                              \
                 python2-avocado{,-plugins-{output-html,varianter-yaml-to-mux}}
    python3 -m pip install --upgrade pip
    python3 -m pip install "avocado-framework<70.0"
    python3 -m pip install "avocado-framework-plugin-result-html<70.0"
    python3 -m pip install "avocado-framework-plugin-varianter-yaml-to-mux<70.0"

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
    # in fact is's on the Leap image twice so remove one
    rm -f $REPOS_DIR/daos-stack-ext-opensuse-15-stable-group.repo
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
