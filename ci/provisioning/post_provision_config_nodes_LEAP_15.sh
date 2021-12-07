#!/bin/bash

REPOS_DIR=/etc/dnf/repos.d
DISTRO_NAME=leap15
# shellcheck disable=SC2034
DISTRO_GENERIC=sl
EXCLUDE_UPGRADE=fuse,fuse-libs,fuse-devel,mercury,daos,daos-\*

bootstrap_dnf() {
    rm -rf "$REPOS_DIR"
    ln -s ../zypp/repos.d "$REPOS_DIR"
}

group_repo_post() {
    # Nothing to do for SL
    :
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

    # shellcheck disable=SC2154
    if ! update_repos "$DISTRO_NAME"; then
        # need to use the image supplied repos
        # shellcheck disable=SC2034
        repo_servers=()
    fi

    time dnf -y repolist

    time dnf repolist

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
        if ! rpm -q "$LSB_RELEASE"; then
            RETRY_COUNT=4 retry_dnf 360 install "$LSB_RELEASE"
        fi
    fi

    # shellcheck disable=SC2001
    if ! rpm -q "$(echo "$INST_RPMS" |
                   sed -e 's/--exclude [^ ]*//'                 \
                       -e 's/[^ ]*-daos-[0-9][0-9]*//g')"; then
        # shellcheck disable=SC2086
        if [ -n "$INST_RPMS" ]; then
            # shellcheck disable=SC2154
            if ! RETRY_COUNT=4 retry_dnf 360 install $INST_RPMS; then
                rc=${PIPESTATUS[0]}
                dump_repos
                exit "$rc"
            fi
        fi
    fi

    distro_custom

    lsb_release -a

    # now make sure everything is fully up-to-date
    # shellcheck disable=SC2154
    if ! RETRY_COUNT=4 retry_dnf 600 upgrade --exclude "$EXCLUDE_UPGRADE"; then
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
