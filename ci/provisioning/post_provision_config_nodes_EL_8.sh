#!/bin/bash

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
    if ! dnf --enablerepo=\*-debuginfo repolist 2>/dev/null | grep -e -debuginfo; then
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
[daos_ci-rocky8-base-artifactory-debuginfo]
name=daos_ci-rocky8-base-artifactory-debuginfo
baseurl=${ARTIFACTORY_URL}artifactory/rocky-\$releasever-proxy/BaseOS/\$arch/debug/tree/
enabled=0
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-rockyofficial

[daos_ci-rocky8-appstream-artifactory-debuginfo]
name=daos_ci-rocky8-appstream-artifactory-debuginfo
baseurl=${ARTIFACTORY_URL}artifactory/rocky-\$releasever-proxy/AppStream/\$arch/debug/tree/
enabled=0
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-rockyofficial

[daos_ci-rocky8-powertools-artifactory-debuginfo]
name=daos_ci-rocky8-powertools-artifactory-debuginfo
baseurl=${ARTIFACTORY_URL}artifactory/rocky-\$releasever-proxy/PowerTools/\$arch/debug/tree/
enabled=0
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-rockyofficial

[daos_ci-rocky8-extras-artifactory-debuginfo]
name=daos_ci-rocky8-extras-artifactory-debuginfo
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
