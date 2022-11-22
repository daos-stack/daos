#!/bin/bash
#
#  (C) Copyright 2021-2022 Intel Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent

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

    # for Launchable's pip install
    dnf -y install python3-setuptools.noarch

}

install_mofed() {
    if [ -z "$MLNX_VER_NUM" ]; then
        echo "MLNX_VER_NUM is not set"
        env
        exit 1
    fi

    # Remove omnipath software
    # shellcheck disable=SC2046
    time dnf -y remove $(rpm -q opa-address-resolution \
                                opa-basic-tools \
                                opa-fastfabric \
                                opa-libopamgt \
                                compat-openmpi16 \
                                compat-openmpi16-devel \
                                openmpi \
                                openmpi-devel \
                                ompi \
                                ompi-debuginfo \
                                ompi-devel | grep -v 'is not installed')


    stream=false
    gversion="$(lsb_release -sr)"
    if [ "$gversion" == "8" ]; then
        gversion="8.6"
        stream=true
     elif [[ $gversion = *.*.* ]]; then
        gversion="${gversion%.*}"
    fi

    # Add a repo to install MOFED RPMS
    repo_url=https://artifactory.dc.hpdd.intel.com/artifactory/mlnx_ofed/"$MLNX_VER_NUM-rhel$gversion"-x86_64/
    dnf -y config-manager --add-repo="$repo_url"
    curl -L -O "$repo_url"RPM-GPG-KEY-Mellanox
    dnf -y config-manager --save --setopt="$(url_to_repo "$repo_url")".gpgcheck=1
    rpm --import RPM-GPG-KEY-Mellanox
    rm -f RPM-GPG-KEY-Mellanox
    dnf repolist || true

    time dnf -y install mlnx-ofed-basic

    # now, upgrade firmware
    time dnf -y install mlnx-fw-updater

    # Make sure that tools are present.
    #ls /usr/bin/ib_* /usr/bin/ibv_*

    dnf list --showduplicates perftest
    if [ "$gversion" == "8.5" ]; then
        dnf remove -y perftest || true
    fi
    if $stream; then
        dnf list --showduplicates ucx-knem
        dnf remove -y ucx-knem || true
    fi

    # Need this module file
    version="$(rpm -q --qf "%{version}" openmpi)"
    mkdir -p /etc/modulefiles/mpi/
    cat << EOF > /etc/modulefiles/mpi/mlnx_openmpi-x86_64
    #%Module 1.0
    #
    #  OpenMPI module for use with 'environment-modules' package:
    #
    conflict		mpi
    prepend-path 		PATH 		/usr/mpi/gcc/openmpi-$version/bin
    prepend-path 		LD_LIBRARY_PATH /usr/mpi/gcc/openmpi-$version/lib64
    prepend-path 		PKG_CONFIG_PATH	/usr/mpi/gcc/openmpi-$version/lib64/pkgconfig
    prepend-path		MANPATH		/usr/mpi/gcc/openmpi-$version/share/man
    setenv 			MPI_BIN		/usr/mpi/gcc/openmpi-$version/bin
    setenv			MPI_SYSCONFIG	/usr/mpi/gcc/openmpi-$version/etc
    setenv			MPI_FORTRAN_MOD_DIR	/usr/mpi/gcc/openmpi-$version/lib64
    setenv			MPI_INCLUDE	/usr/mpi/gcc/openmpi-$version/include
    setenv	 		MPI_LIB		/usr/mpi/gcc/openmpi-$version/lib64
    setenv			MPI_MAN			/usr/mpi/gcc/openmpi-$version/share/man
    setenv			MPI_COMPILER	openmpi-x86_64
    setenv			MPI_SUFFIX	_openmpi
    setenv	 		MPI_HOME	/usr/mpi/gcc/openmpi-$version
EOF

    printf 'MOFED_VERSION=%s\n' "$MLNX_VER_NUM" >> /etc/do-release
}
