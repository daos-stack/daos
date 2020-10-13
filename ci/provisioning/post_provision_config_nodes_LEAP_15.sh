#!/bin/bash

post_provision_config_nodes() {
    # TODO: port this to Zypper
    #       or do we even need it any more?
    #if $CONFIG_POWER_ONLY; then
    #    rm -f /etc/yum.repos.d/*.hpdd.intel.com_job_daos-stack_job_*_job_*.repo
    #    yum -y erase fio fuse ior-hpc mpich-autoload               \
    #                 ompi argobots cart daos daos-client dpdk      \
    #                 fuse-libs libisa-l libpmemobj mercury mpich   \
    #                 openpa pmix protobuf-c spdk libfabric libpmem \
    #                 libpmemblk munge-libs munge slurm             \
    #                 slurm-example-configs slurmctld slurm-slurmmd
    #fi

    # Reserve port ranges 31416-31516 for DAOS and CART servers
    echo 31416-31516 > /proc/sys/net/ipv4/ip_local_reserved_ports

    if [ -n "$DAOS_STACK_GROUP_REPO" ]; then
         # rm -f /etc/yum.repos.d/*"$DAOS_STACK_GROUP_REPO"
        zypper --non-interactive ar                                           \
               "$REPOSITORY_URL"/"$DAOS_STACK_GROUP_REPO" daos-stack-group-repo
        zypper --non-interactive mr --gpgcheck-allow-unsigned-repo \
               daos-stack-group-repo
        rpm --import 'https://download.opensuse.org/repositories/science:/HPC/openSUSE_Leap_15.2/repodata/repomd.xml.key'
        zypper --non-interactive --gpg-auto-import-keys --no-gpg-checks ref \
               daos-stack-group-repo
    fi

    if [ -n "$DAOS_STACK_LOCAL_REPO" ]; then
        zypper --non-interactive ar --gpgcheck-allow-unsigned                 \
               "$REPOSITORY_URL"/"$DAOS_STACK_LOCAL_REPO" daos-stack-local-repo
        zypper --non-interactive mr --no-gpgcheck daos-stack-local-repo
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
            zypper --non-interactive ar --gpgcheck-allow-unsigned "${JENKINS_URL}"job/daos-stack/job/"${repo}"/job/"${branch//\//%252F}"/"${build_number}"/artifact/artifacts/leap15/ "$repo"
        done
    fi

    # TODO: port this to zypper, but do we even need it any more?
    #if [ -n "$INST_RPMS" ]; then
        #yum -y erase $INST_RPMS
    #fi

    # monkey-patch lua-lmod
    if ! grep MODULEPATH=".*"/usr/share/modules /etc/profile.d/lmod.sh; then \
        sed -e '/MODULEPATH=/s/$/:\/usr\/share\/modules/'                     \
               /etc/profile.d/lmod.sh;                                        \
    fi

    # shellcheck disable=SC2086
    if [ -n "$INST_RPMS" ] && \
       ! zypper --non-interactive in $INST_RPMS; then
        rc=${PIPESTATUS[0]}
        for file in /etc/zypp/repos.d/*.repo; do
            echo "---- $file ----"
            cat "$file"
        done
        exit "$rc"
    fi

    # now make sure everything is fully up-to-date
    zypper addlock daos daos-\*
    time zypper --non-interactive up
}
