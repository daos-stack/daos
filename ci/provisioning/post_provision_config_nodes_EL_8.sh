#!/bin/bash

REPOS_DIR=/etc/yum.repos.d
DISTRO_NAME=centos8
DISTRO_GENERIC=el
# shellcheck disable=SC2034
LSB_RELEASE=redhat-lsb-core
EXCLUDE_UPGRADE=dpdk,fuse,mercury,daos,daos-\*

bootstrap_dnf() {
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

    # New Rocky images don't have debuginfo baked into them
    if [ "$(lsb_release -s -i)" = "Rocky" ]; then
        # Need to remove the upstream [debuginfo] repos
        # But need to have the files present so that re-installation is blocked
        for file in /etc/yum.repos.d/{Rocky-Debuginfo,epel{,{,-testing}-modular}}.repo; do
            true > $file
        done

        # add local debuginfo repos
        if [ -f /etc/yum.repos.d/daos_ci-rocky8-artifactory.repo ]; then
            echo >> /etc/yum.repos.d/daos_ci-rocky8-artifactory.repo
        fi

        cat <<EOF >> /etc/yum.repos.d/daos_ci-rocky8-artifactory.repo
[daos_ci-rocky8-base-nexus-debuginfo]
name=daos_ci-rocky8-base-nexus-debuginfo
baseurl=${ARTIFACTORY_URL}artifactory/rocky-\$releasever-proxy/BaseOS/\$arch/debug/tree/
enabled=0
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-rockyofficial

[daos_ci-rocky8-appstream-nexus-debuginfo]
name=daos_ci-rocky8-appstream-nexus-debuginfo
baseurl=${ARTIFACTORY_URL}artifactory/rocky-\$releasever-proxy/AppStream/\$arch/debug/tree/
enabled=0
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-rockyofficial

[daos_ci-rocky8-powertools-nexus-debuginfo]
name=daos_ci-rocky8-powertools-nexus-debuginfo
baseurl=${ARTIFACTORY_URL}artifactory/rocky-\$releasever-proxy/PowerTools/\$arch/debug/tree/
enabled=0
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-rockyofficial

[daos_ci-rocky8-extras-nexus-debuginfo]
name=daos_ci-rocky8-extras-nexus-debuginfo
baseurl=${ARTIFACTORY_URL}artifactory/rocky-\$releasever-proxy/extras/\$arch/debug/tree/
enabled=0
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-rockyofficial
EOF
    else
        if [ -f /etc/yum.repos.d/daos_ci-centos8.repo ]; then
            echo >> /etc/yum.repos.d/daos_ci-centos8.repo
        fi

        cat <<EOF >> /etc/yum.repos.d/daos_ci-centos8.repo
[daos_ci-centos8-artifactory-debuginfo]
name=daos_ci-centos8-artifactory-debuginfo
baseurl=${ARTIFACTORY_URL}artifactory/centos-debuginfo-proxy/\$releasever/\$basearch/
enabled=0
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-centosofficial
EOF
    fi

    # Mellanox OFED hack
    if ls -d /usr/mpi/gcc/openmpi-*; then
        version="$(rpm -q --qf "%{version}" openmpi)"
        mkdir -p /etc/modulefiles/mpi/
        cat <<EOF > /etc/modulefiles/mpi/mlnx_openmpi-x86_64
#%Module 1.0
#
#  OpenMPI module for use with 'environment-modules' package:
#
conflict		mpi
prepend-path 		PATH 		/usr/mpi/gcc/openmpi-${version}/bin
prepend-path 		LD_LIBRARY_PATH /usr/mpi/gcc/openmpi-${version}/lib64
prepend-path 		PKG_CONFIG_PATH	/usr/mpi/gcc/openmpi-${version}/lib64/pkgconfig
prepend-path		MANPATH		/usr/mpi/gcc/openmpi-${version}/share/man
setenv 			MPI_BIN		/usr/mpi/gcc/openmpi-${version}/bin
setenv			MPI_SYSCONFIG	/usr/mpi/gcc/openmpi-${version}/etc
setenv			MPI_FORTRAN_MOD_DIR	/usr/mpi/gcc/openmpi-${version}/lib64
setenv			MPI_INCLUDE	/usr/mpi/gcc/openmpi-${version}/include
setenv	 		MPI_LIB		/usr/mpi/gcc/openmpi-${version}/lib64
setenv			MPI_MAN			/usr/mpi/gcc/openmpi-${version}/share/man
setenv			MPI_COMPILER	openmpi-x86_64
setenv			MPI_SUFFIX	_openmpi
setenv	 		MPI_HOME	/usr/mpi/gcc/openmpi-${version}
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
