#!/bin/bash

REPOS_DIR=/etc/yum.repos.d
DISTRO_NAME=centos7
LSB_RELEASE=redhat-lsb-core
EXCLUDE_UPGRADE=fuse,mercury,daos,daos-\*

bootstrap_dnf() {
    timeout_cmd 5m yum -y install dnf 'dnf-command(config-manager)'
}

group_repo_post() {
    # Nothing to do for EL
    :
}

distro_custom() {
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

    # Mellanox OFED hack
    if ls -d /usr/mpi/gcc/openmpi-*; then
        mkdir -p /etc/modulefiles/mpi/
        cat <<EOF > /etc/modulefiles/mpi/mlnx_openmpi-x86_64
#%Module 1.0
#
#  OpenMPI module for use with 'environment-modules' package:
#
conflict		mpi
prepend-path 		PATH 		/usr/mpi/gcc/openmpi-4.1.0rc5/bin
prepend-path 		LD_LIBRARY_PATH /usr/mpi/gcc/openmpi-4.1.0rc5/lib64
prepend-path 		PKG_CONFIG_PATH	/usr/mpi/gcc/openmpi-4.1.0rc5/lib64/pkgconfig
prepend-path		PYTHONPATH	/usr/lib64/python2.7/site-packages/openmpi
prepend-path		MANPATH		/usr/mpi/gcc/openmpi-4.1.0rc5/share/man
setenv 			MPI_BIN		/usr/mpi/gcc/openmpi-4.1.0rc5/bin
setenv			MPI_SYSCONFIG	/usr/mpi/gcc/openmpi-4.1.0rc5/etc
setenv			MPI_FORTRAN_MOD_DIR	/usr/mpi/gcc/openmpi-4.1.0rc5/lib64
setenv			MPI_INCLUDE	/usr/mpi/gcc/openmpi-4.1.0rc5/include
setenv	 		MPI_LIB		/usr/mpi/gcc/openmpi-4.1.0rc5/lib64
setenv			MPI_MAN			/usr/mpi/gcc/openmpi-4.1.0rc5/share/man
setenv			MPI_PYTHON_SITEARCH	/usr/lib64/python2.7/site-packages/openmpi
setenv			MPI_PYTHON2_SITEARCH	/usr/lib64/python2.7/site-packages/openmpi
setenv			MPI_COMPILER	openmpi-x86_64
setenv			MPI_SUFFIX	_openmpi
setenv	 		MPI_HOME	/usr/mpi/gcc/openmpi-4.1.0rc5
EOF
    fi

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
