#!/bin/bash

post_provision_config_nodes() {
    if $CONFIG_POWER_ONLY; then
        rm -f /etc/yum.repos.d/*.hpdd.intel.com_job_daos-stack_job_*_job_*.repo
        yum -y erase fio fuse ior-hpc mpich-autoload               \
                     ompi argobots cart daos daos-client dpdk      \
                     fuse-libs libisa-l libpmemobj mercury mpich   \
                     openpa pmix protobuf-c spdk libfabric libpmem \
                     libpmemblk munge-libs munge slurm             \
                     slurm-example-configs slurmctld slurm-slurmmd
    fi
    # YUM database on snapshots can be quite old, refresh it before
    # doing anything more
    yum -y makecache
    yum -y install yum-utils ed nfs-utils
    if [ -n "$DAOS_STACK_GROUP_REPO" ]; then
         rm -f /etc/yum.repos.d/*"$DAOS_STACK_GROUP_REPO"
         yum-config-manager --add-repo="$REPOSITORY_URL"/"$DAOS_STACK_GROUP_REPO"
    fi
    
    if [ -n "$DAOS_STACK_LOCAL_REPO" ]; then
        rm -f /etc/yum.repos.d/*"$DAOS_STACK_LOCAL_REPO"
        yum-config-manager --add-repo="$REPOSITORY_URL"/"$DAOS_STACK_LOCAL_REPO"
        echo "gpgcheck = False" >> /etc/yum.repos.d/*"${DAOS_STACK_LOCAL_REPO//\//_}".repo
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
        yum -y erase $INST_RPMS
    fi
    for gpg_url in $GPG_KEY_URLS; do
      rpm --import "$gpg_url"
    done
    rm -f /etc/profile.d/openmpi.sh
    rm -f /tmp/daos_control.log
    yum -y erase metabench mdtest simul IOR compat-openmpi16
    yum -y install epel-release
    if ! yum -y install CUnit python36-PyYAML                         \
                        python36-nose                                 \
                        python36-pip valgrind                         \
                        python36-paramiko                             \
                        python2-avocado                               \
                        python2-avocado-plugins-output-html           \
                        python2-avocado-plugins-varianter-yaml-to-mux \
                        libcmocka python-pathlib                      \
                        python2-numpy git                             \
                        python2-clustershell                          \
                        golang-bin ipmctl ndctl                       \
                        patch $INST_RPMS; then
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
}
