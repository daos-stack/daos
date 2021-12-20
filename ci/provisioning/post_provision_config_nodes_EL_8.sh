#!/bin/bash

REPOS_DIR=/etc/yum.repos.d
DISTRO_NAME=centos8
LSB_RELEASE=redhat-lsb-core
EXCLUDE_UPGRADE=dpdk,fuse,mercury,daos,daos-\*

bootstrap_dnf() {
    # hack in the removal of group repos
    version="$(lsb_release -sr)"
    version=${version%.*}
    if dnf repolist | grep "repo.dc.hpdd.intel.com_repository_centos-${version}-x86_64-group_"; then
        rm -f /etc/yum.repos.d/repo.dc.hpdd.intel.com_repository_{centos-"${version}"-x86_64,daos-stack-centos-8-x86_64-stable}-group_.repo
        for repo in centos-${version}-{base,extras,powertools} epel-el-8; do
            my_repo="${REPOSITORY_URL}repository/$repo-x86_64-proxy"
            my_name="${my_repo#*//}"
            my_name="${my_name//\//_}"
            echo -e "[${my_name}]
name=created from ${my_repo}
baseurl=${my_repo}
enabled=1
repo_gpgcheck=0
gpgcheck=1" >> /etc/yum.repos.d/local-centos-"$repo".repo
        done
        my_repo="${REPOSITORY_URL}/repository/daos-stack-el-8-x86_64-stable-local"
        my_name="${my_repo#*//}"
        my_name="${my_name//\//_}"
        echo -e "[${my_name}]
name=created from ${my_repo}
baseurl=${my_repo}
enabled=1
repo_gpgcheck=0
gpgcheck=0" >> /etc/yum.repos.d/local-daos-group.repo
    fi

    # provisionNodes() will fall-back and install the version without the
    # point release if it fails to install an image with a point release
    # so update the version (should be $releasever) in the repo files and
    # dnf upgrade will take care of the rest

    # but don't do it on the RPM test stages as they are meant to run on
    # specific version
    if [[ ${STAGE_NAME:-} != Test\ CentOS\ 8.*\ RPMs ]]; then
        # use the EL8-version: commit pragma version if specified
        version="$(echo "$COMMIT_MESSAGE" | sed -ne '/^EL8-version: */s/.*: *//p')"
        if [ -n "$version" ]; then
            if [ "$version" != "8.5.2111" ] && [[ $version = *.*.* ]]; then
                version=${version%.*}
            fi
            cur_version="$(lsb_release -sr)"
            if [ "$cur_version" != "8.5.2111" ] && [[ $cur_version = *.*.* ]]; then
                cur_version=${cur_version%.*}
            fi
            # this should be a NOOP if the test image is for the EL8-version requested
            # if not, this will cause it to be upgraded to that version
            # shellcheck disable=SC1087
            sed -E -i -e "s/((\/|_)centos-)$cur_version[^-]*/\1$version/g" /etc/yum.repos.d/*.repo
        fi
    fi

    dnf repolist

    # perftest causes MOFED/distro conflicts:
    # Problem: package perftest-4.5-1.el8.x86_64 requires libefa.so.1()(64bit), but none of the providers can be installed
    #  - package perftest-4.5-1.el8.x86_64 requires libefa.so.1(EFA_1.1)(64bit), but none of the providers can be installed
    #  - cannot install both libibverbs-35.0-1.el8.x86_64 and libibverbs-54mlnx1-1.54103.x86_64
    #  - cannot install the best update candidate for package perftest-4.5-0.6.gbb9a707.54103.x86_64
    #  - problem with installed package libibverbs-54mlnx1-1.54103.x86_64
    if rpm -q perftest; then
        dnf -y erase perftest
    fi

    systemctl enable postfix.service
    systemctl start postfix.service
}

group_repo_post() {
    # Nothing to do for EL
    :
}

distro_custom() {
    # install avocado
    dnf -y install python3-avocado{,-plugins-{output-html,varianter-yaml-to-mux}} \
                   clustershell

    dnf config-manager --disable powertools
    # Mellanox OFED hack
    if ls -d /usr/mpi/gcc/openmpi-*; then
        mkdir -p /etc/modulefiles/mpi/
        cat <<EOF > /etc/modulefiles/mpi/mlnx_openmpi-x86_64
#%Module 1.0
#
#  OpenMPI module for use with 'environment-modules' package:
#
conflict		mpi
prepend-path 		PATH 		/usr/mpi/gcc/openmpi-4.1.2a1/bin
prepend-path 		LD_LIBRARY_PATH /usr/mpi/gcc/openmpi-4.1.2a1/lib64
prepend-path 		PKG_CONFIG_PATH	/usr/mpi/gcc/openmpi-4.1.2a1/lib64/pkgconfig
prepend-path		PYTHONPATH	/usr/lib64/python2.7/site-packages/openmpi
prepend-path		MANPATH		/usr/mpi/gcc/openmpi-4.1.2a1/share/man
setenv 			MPI_BIN		/usr/mpi/gcc/openmpi-4.1.2a1/bin
setenv			MPI_SYSCONFIG	/usr/mpi/gcc/openmpi-4.1.2a1/etc
setenv			MPI_FORTRAN_MOD_DIR	/usr/mpi/gcc/openmpi-4.1.2a1/lib64
setenv			MPI_INCLUDE	/usr/mpi/gcc/openmpi-4.1.2a1/include
setenv	 		MPI_LIB		/usr/mpi/gcc/openmpi-4.1.2a1/lib64
setenv			MPI_MAN			/usr/mpi/gcc/openmpi-4.1.2a1/share/man
setenv			MPI_PYTHON_SITEARCH	/usr/lib64/python2.7/site-packages/openmpi
setenv			MPI_PYTHON2_SITEARCH	/usr/lib64/python2.7/site-packages/openmpi
setenv			MPI_COMPILER	openmpi-x86_64
setenv			MPI_SUFFIX	_openmpi
setenv	 		MPI_HOME	/usr/mpi/gcc/openmpi-4.1.2a1
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

    time dnf repolist
    # the group repo is always on the test image
    #add_group_repo
    #add_local_repo

    # CORCI-1096
    # workaround until new snapshot images are produced
    # Assume if APPSTREAM is locally proxied so is epel-modular
    # so disable the upstream epel-modular repo
    : "${DAOS_STACK_EL_8_APPSTREAM_REPO:-}"
    if [ -n "${DAOS_STACK_EL_8_APPSTREAM_REPO}" ]; then
        dnf config-manager --disable epel-modular appstream powertools
    fi
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
    retry_cmd 360 dnf -y install $LSB_RELEASE

    # shellcheck disable=SC2086
    if [ -n "$INST_RPMS" ]; then
        if ! retry_cmd 360 dnf -y install $INST_RPMS; then
            rc=${PIPESTATUS[0]}
            dump_repos
            exit "$rc"
        fi
    fi

    distro_custom

    # now make sure everything is fully up-to-date
    if ! retry_cmd 600 dnf -y upgrade --exclude "$EXCLUDE_UPGRADE"; then
        dump_repos
        exit 1
    fi

    # show the final version once dnf upgrade is done
    lsb_release -a

    if [ -f /etc/do-release ]; then
        cat /etc/do-release
    fi
    cat /etc/os-release
    
    ofed_info || true

    exit 0
}
