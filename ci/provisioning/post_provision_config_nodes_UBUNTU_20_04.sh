#!/bin/bash
#
#  (C) Copyright 2021-2023 Intel Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent

post_provision_config_nodes() {
    # should we port this to Ubuntu or just consider $CONFIG_POWER_ONLY dead?
    #if $CONFIG_POWER_ONLY; then
    #    rm -f /etc/yum.repos.d/*.hpdd.intel.com_job_daos-stack_job_*_job_*.repo
    #    yum -y erase fio fuse ior-hpc mpich-autoload               \
    #                 ompi argobots cart daos daos-client dpdk      \
    #                 fuse-libs libisa-l libpmemobj mercury mpich   \
    #                 pmix protobuf-c spdk libfabric libpmem        \
    #                 libpmemblk munge-libs munge slurm             \
    #                 slurm-example-configs slurmctld slurm-slurmmd
    #fi

    # why, Canonical? just why?
    sed -i -e "/$HOSTNAME/d" /etc/hosts

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
    # TODO: investigate why this is needed
    sed -i -e '/daos-stack-daos-ubuntu/d' /etc/apt/sources.list.d/daos_ci-ubuntu*-artifactory.list

    apt-get update

    local inst_rpms=()
    if [ -n "$INST_RPMS" ]; then
        eval "inst_rpms=($INST_RPMS)"
        if ! apt-get -y remove "${inst_rpms[@]}"; then
            rc=${PIPESTATUS[0]}
            if [ "$rc" -ne 100 ]; then
                echo "Error $rc removing $INST_RPMS"
                return "$rc"
            fi
        fi
    fi

    # maybe in the next release of Ubuntu
    #apt-get -y install avocado python3-avocado-plugins-output-html   \
    #                   python3-avocado-plugins-varianter-yaml-to-mux \

    apt-get -y install  lsb-core patchutils
    apt-get -y remove python3-avocado{,-plugins-{varianter-yaml-to-mux,output-html}}

    python3 -m pip install --upgrade pip
    python3 -m pip install "avocado-framework<70.0"
    python3 -m pip install "avocado-framework-plugin-result-html<70.0"
    python3 -m pip install "avocado-framework-plugin-varianter-yaml-to-mux<70.0"

    if [ -n "$INST_RPMS" ] &&
       ! apt-get -y install "${inst_rpms[@]}"; then
        rc=${PIPESTATUS[0]}
        for file in /etc/apt/sources.list{,.d/*.list}; do
            echo "---- $file ----"
            cat "$file"
        done
        return "$rc"
    fi

    # temporary hack until Python 3 is supported by Functional testing
    # possible TODO: support testing non-RPM testing
    sed -ie '1s/2/3/' /usr/lib/daos/TESTING/ftest/launch.py

    # change the default shell to bash -- we write a lot of bash
    chsh -s /bin/bash

    # needed for test_cart_iv
    echo 0 > /proc/sys/kernel/yama/ptrace_scope
    cat /proc/sys/kernel/yama/ptrace_scope
    rm -f /etc/sysctl.d/10-ptrace.conf

    # make sure mpich is the current mpi alternative
    # TODO: should instead just use mpirun.mpich in testing
    update-alternatives --set mpi /usr/bin/mpicc.mpich
    update-alternatives --set mpirun /usr/bin/mpirun.mpich

    return 0
}
