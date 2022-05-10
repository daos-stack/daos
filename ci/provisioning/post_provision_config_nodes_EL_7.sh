#!/bin/bash

if lspci | grep "ConnectX-6"; then
    # No openmpi3 or MACSio-openmpi3 can be installed currently
    # when the ConnnectX-6 driver is installed
    INST_RPMS="${INST_RPMS// openmpi3/}"
    INST_RPMS="${INST_RPMS// MACSio-openmpi3}"
fi

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
    cat <<"EOF" > "$REPOS_DIR"/CentOS-Debuginfo.repo
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
prepend-path		PYTHONPATH	/usr/lib64/python2.7/site-packages/openmpi
prepend-path		MANPATH		/usr/mpi/gcc/openmpi-${version}/share/man
setenv 			MPI_BIN		/usr/mpi/gcc/openmpi-${version}/bin
setenv			MPI_SYSCONFIG	/usr/mpi/gcc/openmpi-${version}/etc
setenv			MPI_FORTRAN_MOD_DIR	/usr/mpi/gcc/openmpi-${version}/lib64
setenv			MPI_INCLUDE	/usr/mpi/gcc/openmpi-${version}/include
setenv	 		MPI_LIB		/usr/mpi/gcc/openmpi-${version}/lib64
setenv			MPI_MAN			/usr/mpi/gcc/openmpi-${version}/share/man
setenv			MPI_PYTHON_SITEARCH	/usr/lib64/python2.7/site-packages/openmpi
setenv			MPI_PYTHON2_SITEARCH	/usr/lib64/python2.7/site-packages/openmpi
setenv			MPI_COMPILER	openmpi-x86_64
setenv			MPI_SUFFIX	_openmpi
setenv	 		MPI_HOME	/usr/mpi/gcc/openmpi-${version}
EOF
    fi
}
