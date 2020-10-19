#!/bin/bash

post_provision_config_nodes() {
    local yum_repo_args="--disablerepo=*"
    yum_repo_args+=" --enablerepo=repo.dc.hpdd.intel.com_repository_*"
    yum_repo_args+=",build.hpdd.intel.com_job_daos-stack*"

    # Reserve port ranges for DAOS and CART servers
    echo 31416-31516 > /proc/sys/net/ipv4/ip_local_reserved_ports

    if $CONFIG_POWER_ONLY; then
        rm -f /etc/yum.repos.d/*.hpdd.intel.com_job_daos-stack_job_*_job_*.repo
        yum -y erase fio fuse ior-hpc mpich-autoload               \
                     ompi argobots cart daos daos-client dpdk      \
                     fuse-libs libisa-l libpmemobj mercury mpich   \
                     openpa pmix protobuf-c spdk libfabric libpmem \
                     libpmemblk munge-libs munge slurm             \
                     slurm-example-configs slurmctld slurm-slurmmd
    fi
    if [ -n "$DAOS_STACK_GROUP_REPO" ]; then
         rm -f /etc/yum.repos.d/*"$DAOS_STACK_GROUP_REPO"
         yum-config-manager \
             --add-repo="$REPOSITORY_URL"/"$DAOS_STACK_GROUP_REPO"
    fi

    if [ -n "$DAOS_STACK_LOCAL_REPO" ]; then
        rm -f /etc/yum.repos.d/*"$DAOS_STACK_LOCAL_REPO"
        yum-config-manager --add-repo="$REPOSITORY_URL"/"$DAOS_STACK_LOCAL_REPO"
        echo "gpgcheck = False" >> \
            /etc/yum.repos.d/*"${DAOS_STACK_LOCAL_REPO//\//_}".repo
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
            yum-config-manager --add-repo="${JENKINS_URL}"job/daos-stack/job/"${repo}"/job/"${branch//\//%252F}"/"${build_number}"/artifact/artifacts/centos7/
            pname=$(ls /etc/yum.repos.d/*.hpdd.intel.com_job_daos-stack_job_"${repo}"_job_"${branch//\//%252F}"_"${build_number}"_artifact_artifacts_centos7_.repo)
            if [ "$pname" != "${pname//%252F/_}" ]; then
                mv "$pname" "${pname//%252F/_}"
            fi
            pname="${pname//%252F/_}"
            sed -i -e '/^\[/s/%252F/_/g' -e '$s/^$/gpgcheck = False/' "$pname"
            cat "$pname"
        done
    fi
    if [ -n "$INST_RPMS" ]; then
        # shellcheck disable=SC2086
        yum -y erase $INST_RPMS
    fi
    for gpg_url in $GPG_KEY_URLS; do
      rpm --import "$gpg_url"
    done
    rm -f /etc/profile.d/openmpi.sh
    rm -f /tmp/daos_control.log
    # shellcheck disable=SC2086
    if [ -n "$INST_RPMS" ] &&
       ! yum -y $yum_repo_args install $INST_RPMS; then
        rc=${PIPESTATUS[0]}
        for file in /etc/yum.repos.d/*.repo; do
            echo "---- $file ----"
            cat "$file"
        done
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
    cat <<"EOF" > /etc/yum.repos.d/CentOS-Debuginfo.repo
[core-0-debuginfo]
name=CentOS-7 - Debuginfo
baseurl=http://debuginfo.centos.org/7/$basearch/
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-CentOS-Debug-7
enabled=0
EOF

    # now make sure everything is fully up-to-date
    time yum -y upgrade --exclude fuse,mercury,daos,daos-\*
}
