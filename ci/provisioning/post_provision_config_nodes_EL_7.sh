#!/bin/bash

REPOS_DIR=/etc/yum.repos.d
DISTRO_NAME=centos7
LSB_RELEASE=redhat-lsb-core
EXCLUDE_UPGRADE=fuse,mercury,daos,daos-\*

timeout_yum() {
    local timeout="$1"
    shift

    # now make sure everything is fully up-to-date
    local tries=3
    while [ $tries -gt 0 ]; do
        if time timeout "$timeout" yum -y "$@"; then
            # succeeded, return with success
            return 0
        fi
        if [ "${PIPESTATUS[0]}" = "124" ]; then
            # timed out, try again
            (( tries-- ))
            continue
        fi
        # yum failed for something other than timeout
        return 1
    done

    return 1
}

bootstrap_dnf() {
    timeout_yum 5m install dnf 'dnf-command(config-manager)'
}

group_repo_post() {
    # Nothing to do for EL
    :
}

distro_custom() {
    # shellcheck disable=SC2086
    time dnf -y install python3

    if [ ! -e /usr/bin/pip3 ] &&
       [ -e /usr/bin/pip3.6 ]; then
        ln -s pip3.6 /usr/bin/pip3
    fi
    if [ ! -e /usr/bin/python3 ] &&
       [ -e /usr/bin/python3.6 ]; then
        ln -s python3.6 /usr/bin/python3
    fi

    # install the debuginfo repo in case we get segfaults
    cat <<"EOF" > $REPOS_DIR/CentOS-Debuginfo.repo
[core-0-debuginfo]
name=CentOS-7 - Debuginfo
baseurl=http://debuginfo.centos.org/7/$basearch/
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-CentOS-Debug-7
enabled=0
EOF

    # force install of avocado 69.x
    dnf -y erase avocado{,-common}                                              \
                 python2-avocado{,-plugins-{output-html,varianter-yaml-to-mux}} \
                 python36-PyYAML
    pip3 install --upgrade pip
    pip3 install "avocado-framework<70.0"
    pip3 install "avocado-framework-plugin-result-html<70.0"
    pip3 install "avocado-framework-plugin-varianter-yaml-to-mux<70.0"
    pip3 install clustershell
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

    time dnf repolist
    # the group repo is always on the test image
    #add_group_repo
    add_local_repo
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
            dnf config-manager --add-repo="${repo_url}"
            disable_gpg_check "$repo_url"
        done
    fi
    if [ -n "$INST_RPMS" ]; then
        # shellcheck disable=SC2086
        time dnf -y erase $INST_RPMS
    fi
    rm -f /etc/profile.d/openmpi.sh
    rm -f /tmp/daos_control.log
    time dnf -y install $LSB_RELEASE

    # shellcheck disable=SC2086
    if [ -n "$INST_RPMS" ] &&
       ! time dnf -y install $INST_RPMS; then
        rc=${PIPESTATUS[0]}
        dump_repos
        exit "$rc"
    fi

    distro_custom

    # now make sure everything is fully up-to-date
    if ! time dnf -y upgrade \
                  --exclude "$EXCLUDE_UPGRADE"; then
        dump_repos
        exit 1
    fi

    if [ -f /etc/do-release ]; then
        cat /etc/do-release
    fi
    cat /etc/os-release

    exit 0
}
