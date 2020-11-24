#!/bin/bash

post_provision_config_nodes() {
    # should we port this to Ubuntu or just consider $CONFIG_POWER_ONLY dead?
    #if $CONFIG_POWER_ONLY; then
    #    rm -f /etc/yum.repos.d/*.hpdd.intel.com_job_daos-stack_job_*_job_*.repo
    #    yum -y erase fio fuse ior-hpc mpich-autoload               \
    #                 ompi argobots cart daos daos-client dpdk      \
    #                 fuse-libs libisa-l libpmemobj mercury mpich   \
    #                 openpa pmix protobuf-c spdk libfabric libpmem \
    #                 libpmemblk munge-libs munge slurm             \
    #                 slurm-example-configs slurmctld slurm-slurmmd
    #fi
    codename=$(lsb_release -s -c)
    if [ -n "$DAOS_STACK_GROUP_REPO" ]; then
        add-apt-repository \
            "deb $REPOSITORY_URL/$DAOS_STACK_GROUP_REPO $codename"
    fi

    if [ -n "$DAOS_STACK_LOCAL_REPO" ]; then
        echo "deb [trusted=yes] $REPOSITORY_URL/$DAOS_STACK_LOCAL_REPO $codename main" >> /etc/apt/sources.list
    fi

    if [ -n "$INST_REPOS" ]; then
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
            echo "deb [trusted=yes] ${JENKINS_URL}job/daos-stack/job/${repo}/job/${branch//\//%252F}/${build_number}/artifact/artifacts/ubuntu20.04 ./" >> /etc/apt/sources.list
        done
    fi
    apt-get update
    if [ -n "$INST_RPMS" ]; then
        # shellcheck disable=SC2086
        if ! apt-get -y remove $INST_RPMS; then
            rc=${PIPESTATUS[0]}
            if [ $rc -ne 100 ]; then
                echo "Error $rc removing $INST_RPMS"
                exit $rc
            fi
        fi
    fi

    apt-get -y install avocado python3-avocado-plugins-output-html   \
                       python3-avocado-plugins-varianter-yaml-to-mux \
                       lsb-core

    # shellcheck disable=2086
    if [ -n "$INST_RPMS" ] &&
       ! apt-get -y install $INST_RPMS; then
        rc=${PIPESTATUS[0]}
        for file in /etc/apt/sources.list{,.d/*.list}; do
            echo "---- $file ----"
            cat "$file"
        done
        exit "$rc"
    fi

    # temporary hack until Python 3 is supported by Functional testing
    # possible TODO: support testing non-RPM testing
    sed -ie '1s/2/3/' /usr/lib/daos/TESTING/ftest/launch.py

    # change the default shell to bash -- we write a lot of bash
    chsh -s /bin/bash
}
