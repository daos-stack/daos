#!/bin/bash

REPOS_DIR=/etc/yum.repos.d
DISTRO_NAME=centos7
LSB_RELEASE=redhat-lsb-core

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
    # nothing for EL7
    :
}

post_provision_config_nodes() {
    bootstrap_dnf

    # Reserve port ranges 31416-31516 for DAOS and CART servers
    echo 31416-31516 > /proc/sys/net/ipv4/ip_local_reserved_ports

    if $CONFIG_POWER_ONLY; then
        rm -f $REPOS_DIR/*.hpdd.intel.com_job_daos-stack_job_*_job_*.repo
        dnf -y erase fio fuse ior-hpc mpich-autoload               \
                     ompi argobots cart daos daos-client dpdk      \
                     fuse-libs libisa-l libpmemobj mercury mpich   \
                     openpa pmix protobuf-c spdk libfabric libpmem \
                     libpmemblk munge-libs munge slurm             \
                     slurm-example-configs slurmctld slurm-slurmmd
    fi

    local dnf_repo_args="--disablerepo=*"

    add_repo "$DAOS_STACK_GROUP_REPO"
    group_repo_post

    add_repo "${DAOS_STACK_LOCAL_REPO}" false

    # TODO: this should be per repo for the above two repos
    dnf_repo_args+=" --enablerepo=repo.dc.hpdd.intel.com_repository_*"

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
            # TODO: this should be per repo in the above loop
            if [ -n "$INST_REPOS" ]; then
                dnf_repo_args+=",build.hpdd.intel.com_job_daos-stack*"
            fi
        done
    fi
    if [ -n "$INST_RPMS" ]; then
        # shellcheck disable=SC2086
        dnf -y erase $INST_RPMS
    fi
    rm -f /etc/profile.d/openmpi.sh
    rm -f /tmp/daos_control.log
    dnf -y install $LSB_RELEASE
    # shellcheck disable=SC2086
    if [ -n "$INST_RPMS" ] &&
       ! dnf -y $dnf_repo_args install $INST_RPMS; then
        rc=${PIPESTATUS[0]}
        dump_repos
        exit "$rc"
    fi
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

    # now make sure everything is fully up-to-date
    if ! time dnf -y upgrade \
                  --exclude fuse,mercury,daos,daos-\*; then
        dump_repos
        exit 1
    fi

    exit 0
}
