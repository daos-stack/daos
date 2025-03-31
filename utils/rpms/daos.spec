%define daoshome %{_exec_prefix}/lib/%{name}
%define server_svc_name daos_server.service
%define agent_svc_name daos_agent.service
%define sysctl_script_name 10-daos_server.conf

%define __arch_install_post %{nil}
%global daos_root /opt/daos

%bcond_without server
%bcond_without ucx

%if %{with server}
%global daos_build_args FIRMWARE_MGMT=yes
%else
%global daos_build_args client test
%endif
%global mercury_version   2.4.0-4
%global libfabric_version 1.22.0-2
%global argobots_version 1.2
%global __python %{__python3}

%if (0%{?rhel} >= 8)
# https://bugzilla.redhat.com/show_bug.cgi?id=1955184
%define _use_internal_dependency_generator 0
%define __find_requires %{SOURCE1}
%endif

Name:          daos
Version:       2.7.101
Release:       8%{?relval}%{?dist}
Summary:       DAOS Storage Engine

License:       BSD-2-Clause-Patent
URL:           https://github.com/daos-stack/daos
Source0:       %{name}-%{version}.tar.gz
Source1:       bz-1955184_find-requires
%if (0%{?rhel} >= 8)
BuildRequires: python3-scons >= 2.4
%else
BuildRequires: scons >= 2.4
%endif
BuildRequires: gcc-c++
%if (0%{?rhel} >= 8)
%global openmpi openmpi
%else
%global openmpi openmpi3
%endif
BuildRequires: %{openmpi}-devel
BuildRequires: hwloc-devel
%if ("%{?compiler_args}" == "COMPILER=covc")
BuildRequires: bullseye
%endif
%if (0%{?rhel} >= 8)
BuildRequires: json-c-devel
BuildRequires: boost-python3-devel
%else
BuildRequires: libjson-c-devel
BuildRequires: boost-devel
%endif
%if (0%{?suse_version} >= 1500)
BuildRequires: go-race
BuildRequires: libprotobuf-c-devel
BuildRequires: liblz4-devel
BuildRequires: libcapstone-devel
%else
BuildRequires: protobuf-c-devel
BuildRequires: lz4-devel
BuildRequires: capstone-devel
%endif
%if %{with server}
BuildRequires: libaio-devel
#BuildRequires: meson
%endif
BuildRequires: openssl-devel
BuildRequires: libevent-devel
BuildRequires: libyaml-devel
BuildRequires: libcmocka-devel
BuildRequires: valgrind-devel
BuildRequires: systemd
BuildRequires: go >= 1.21
BuildRequires: pciutils-devel
%if (0%{?rhel} >= 8)
BuildRequires: numactl-devel
BuildRequires: CUnit-devel
# needed to retrieve PMM region info through control-plane
%if %{with server}
BuildRequires: libipmctl-devel
%endif
%if (0%{?rhel} >= 9)
BuildRequires: python-devel
%else
BuildRequires: python36-devel
%endif
BuildRequires: python3-distro
BuildRequires: Lmod
%else
%if (0%{?suse_version} >= 1315)
# see src/client/dfs/SConscript for why we need /etc/os-release
# that code should be rewritten to use the python libraries provided for
# os detection
BuildRequires: distribution-release
BuildRequires: libnuma-devel
BuildRequires: cunit-devel
%if %{with server}
BuildRequires: ipmctl-devel
%endif
BuildRequires: python3-devel
BuildRequires: python3-distro
BuildRequires: python-rpm-macros
BuildRequires: lua-lmod
BuildRequires: systemd-rpm-macros
%endif
%endif
BuildRequires: libuuid-devel

# Needed for debugging tasks
%if (0%{?rhel} >= 8)
BuildRequires: libasan
%endif
%if (0%{?suse_version} > 0)
BuildRequires: libasan8
%endif

Requires: openssl


%description
The Distributed Asynchronous Object Storage (DAOS) is an open-source
software-defined object store designed from the ground up for
massively distributed Non Volatile Memory (NVM). DAOS takes advantage
of next generation NVM technology like Storage Class Memory (SCM) and
NVM express (NVMe) while presenting a key-value storage interface and
providing features such as transactional non-blocking I/O, advanced
data protection with self healing on top of commodity hardware, end-
to-end data integrity, fine grained data control and elastic storage
to optimize performance and cost.

%if %{with server}
%package server-deps-common
Summary: Prebuilt server dependencies

%description server-deps-common
Prebuilt server dependencies needed for server components.  Includes
SPDK and PMDK libraries

%package server
Summary: The DAOS server
Requires: %{name}%{?_isa} = %{version}-%{release}
Requires: %{name}-deps-common%{?_isa} = %{version}-%{release}
Requires: %{name}-server-deps-common%{?_isa} = %{version}-%{release}
Requires: ndctl
# needed to set PMem configuration goals in BIOS through control-plane
%if (0%{?suse_version} >= 1500)
Requires: ipmctl >= 03.00.00.0423
%else
Requires: ipmctl >= 03.00.00.0468
%endif
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
Requires: numactl
Requires: pciutils
%{?systemd_requires}

%description server
This is the package needed to run a DAOS server
%endif

%package admin
Summary: DAOS admin tools
Requires: %{name}%{?_isa} = %{version}-%{release}

%description admin
This package contains DAOS administrative tools (e.g. dmg).

%if (0%{?suse_version} > 0)
%define libfabric_name libfabric1
%else
%define libfabric_name libfabric
%endif

%package        %{libfabric_name}
Summary:        Shared library for libfabric
Group:          System/Libraries
Provides:       %{libfabric_name}%{?_isa} = %{libfabric_version}

%description %{libfabric_name}
%{name}-%{libfabric_name} provides a user-space API to access high-performance fabric
services, such as RDMA. This package contains the runtime library.

%package        %{libfabric_name}-devel
Summary:        Development files for %{name}
Group:          Development/Libraries/C and C++
Provides:       %{libfabric_name}-devel%{?_isa} = %{libfabric_version}
Requires:       %{name}-%{libfabric_name}%{?_isa} = %{version}-%{release}

%description    %{libfabric_name}-devel
The %{libfabric_name}-devel package contains libraries and header files for
developing applications that use %{libfabric_name}.

%package mercury
Summary:  Mercury package
Provides: mercury%{?_isa}-%{mercury_version}

%description mercury
Mercury is a Remote Procedure Call (RPC) framework specifically
designed for use in High-Performance Computing (HPC) systems with
high-performance fabrics. Its network implementation is abstracted
to make efficient use of native transports and allow easy porting
to a variety of systems. Mercury supports asynchronous transfer of
parameters and execution requests, and has dedicated support for
large data arguments that are transferred using Remote Memory
Access (RMA). Its interface is generic and allows any function
call to be serialized. Since code generation is done using the C
preprocessor, no external tool is required.

%package mercury-devel
Summary:  Mercury devel package
Provides: mercury-devel%{?_isa}-%{mercury_version}
Requires: mercury%{?_isa} = %{mercury_version}

%description mercury-devel
Mercury development headers and libraries.

%if %{with ucx}
%package mercury-ucx
Summary:  Mercury with UCX
Provides: mercury-ucx%{?_isa}-%{mercury_version}
Requires: mercury%{?_isa} = %{mercury_version}

%description mercury-ucx
Mercury plugin to support the UCX transport.
%endif

%package client
Summary: The DAOS client
Requires: %{name}%{?_isa} = %{version}-%{release}
Requires: /usr/bin/fusermount3
%{?systemd_requires}

%description client
This is the package needed to run a DAOS client

%package tests
Summary: The entire DAOS test suite
Requires: %{name}-client-tests%{?_isa} = %{version}-%{release}
BuildArch: noarch

%description tests
This is the package is a metapackage to install all of the test packages

%package tests-internal
Summary: The entire internal DAOS test suite
Requires: %{name}-tests = %{version}-%{release}
Requires: %{name}-client-tests-openmpi%{?_isa} = %{version}-%{release}
Requires: %{name}-client-tests-mpich = %{version}-%{release}
Requires: %{name}-serialize%{?_isa} = %{version}-%{release}
BuildArch: noarch

%description tests-internal
This is the package is a metapackage to install all of the internal test
packages

%package deps-common
Summary: Common prebuilt dependencies

%description deps-common
Builds Argobots for tests and server components

%package client-tests
Summary: The DAOS test suite
Requires: %{name}-client%{?_isa} = %{version}-%{release}
Requires: %{name}-admin%{?_isa} = %{version}-%{release}
Requires: %{name}-devel%{?_isa} = %{version}-%{release}
Requires: %{name}-deps-common%{?_isa} = %{version}-%{release}
%if (0%{?suse_version} >= 1500)
Requires: libprotobuf-c-devel
%else
Requires: protobuf-c-devel
%endif
Requires: fio
Requires: git
Requires: dbench
Requires: lbzip2
Requires: attr
Requires: ior
Requires: go >= 1.21
%if (0%{?suse_version} >= 1315)
Requires: lua-lmod
Requires: libcapstone-devel
%else
Requires: Lmod
Requires: capstone-devel
%endif
Requires: pciutils-devel
%if (0%{?suse_version} > 0)
Requires: libndctl-devel
%endif
%if (0%{?rhel} >= 8)
Requires: ndctl-devel
Requires: daxctl-devel
%endif

%description client-tests
This is the package needed to run the DAOS test suite (client tests)

%package client-tests-openmpi
Summary: The DAOS client test suite - tools which need openmpi
Requires: %{name}-client-tests%{?_isa} = %{version}-%{release}
Requires: hdf5-%{openmpi}-tests
Requires: hdf5-vol-daos-%{openmpi}-tests
Requires: MACSio-%{openmpi}
Requires: simul-%{openmpi}

%description client-tests-openmpi
This is the package needed to run the DAOS client test suite openmpi tools

%package client-tests-mpich
Summary: The DAOS client test suite - tools which need mpich
BuildArch: noarch
Requires: %{name}-client-tests%{?_isa} = %{version}-%{release}
Requires: mpifileutils-mpich
Requires: testmpio
Requires: mpich
Requires: ior
Requires: hdf5-mpich-tests
Requires: hdf5-vol-daos-mpich-tests
Requires: MACSio-mpich
Requires: simul-mpich
Requires: romio-tests
Requires: python3-mpi4py-tests

%description client-tests-mpich
This is the package needed to run the DAOS client test suite mpich tools

%if %{with server}
%package server-tests
Summary: The DAOS server test suite (server tests)
Requires: %{name}-server%{?_isa} = %{version}-%{release}
Requires: %{name}-admin%{?_isa} = %{version}-%{release}

%description server-tests
This is the package needed to run the DAOS server test suite (server tests)
%endif

%package devel
Summary: The DAOS development libraries and headers
Requires: %{name}-client%{?_isa} = %{version}-%{release}
Requires: libuuid-devel

%description devel
This is the package needed to build software with the DAOS library.

%if %{with server}
%package firmware
Summary: The DAOS firmware management helper
Requires: %{name}-server%{?_isa} = %{version}-%{release}

%description firmware
This is the package needed to manage server storage firmware on DAOS servers.
%endif

%package serialize
Summary: DAOS serialization library that uses HDF5
BuildRequires: hdf5-devel
Requires: hdf5

%description serialize
This is the package needed to use the DAOS serialization and deserialization
tools, as well as the preserve option for the filesystem copy tool.

%package mofed-shim
Summary: A shim to bridge MOFED's openmpi to distribution dependency tags
Provides: libmpi.so.40()(64bit)(openmpi-x86_64)
Requires: libmpi.so.40()(64bit)
Provides: libmpi_cxx.so.40()(64bit)(openmpi-x86_64)
Provides: libmpi_cxx.so.40()(64bit)
BuildArch: noarch

%description mofed-shim
This is the package that bridges the difference between the MOFED openmpi
"Provides" and distribution-openmpi consumers "Requires".

%if (0%{?suse_version} > 0)
%global __debug_package 1
%global _debuginfo_subpackages 1
%debug_package
%endif

%prep
%autosetup -p1

%build

%install
%define conf_dir %{_sysconfdir}/daos
%if (0%{?rhel} == 8)
%define scons_exe scons-3
%else
%define scons_exe scons
%endif
%{scons_exe} %{?_smp_mflags}          \
      --config=force                  \
      --install-sandbox=%{buildroot}  \
      %{buildroot}%{daos_root}          \
      TARGET_TYPE=release               \
      USE_INSTALLED=ucx,isal,isal_crypto \
      --build-deps=yes                \
      PREFIX=%{daos_root}              \
     %{?daos_build_args}            \
      %{?scons_args}                  \
      %{?compiler_args}

# Move mercury build
utils/rpms/move_prereq.sh mercury "%{buildroot}" "%{_prefix}" "%{_bindir}" "%{_libdir}" \
                                  "%{_includedir}" "%{_sysconfdir}" %{_datadir} lib
utils/rpms/move_prereq.sh ofi "%{buildroot}" "%{_prefix}" "%{_bindir}" "%{_libdir}" \
                              "%{_includedir}" "%{_sysconfdir}" %{_datadir} lib
utils/rpms/move_files.sh "%{buildroot}" "%{buildroot}%{daos_root}/bin" "%{buildroot}%{_bindir}" \
                         "%{buildroot}%{daos_root}" "%{buildroot}%{_prefix}" "lib64" "%{_libdir}" \
                         "daos" \
                         "daos_agent" \
                         "cart_ctl" \
                         "self_test" \
                         "dfuse" \
                         "dmg"
utils/rpms/move_files.sh "%{buildroot}" "%{buildroot}%{daos_root}/lib64" "%{buildroot}%{_libdir}" \
                         "%{buildroot}%{daos_root}" "%{buildroot}%{_prefix}" "lib64" "%{_libdir}" \
                         "daos" \
                         $(basename -a "%{buildroot}%{daos_root}/lib64/libgurt"*) \
                         $(basename -a "%{buildroot}%{daos_root}/lib64/libcart"*) \
                         $(basename -a "%{buildroot}%{daos_root}/lib64/libdaos_serialize"*) \
                         $(basename -a "%{buildroot}%{daos_root}/lib64/libdfs"*) \
                         $(basename -a "%{buildroot}%{daos_root}/lib64/libdaos."*) \
                         $(basename -a "%{buildroot}%{daos_root}/lib64/libduns"*) \
                         $(basename -a "%{buildroot}%{daos_root}/lib64/libds3"*) \
                         $(basename -a "%{buildroot}%{daos_root}/lib64/libdaos_cmd_hdlrs"*) \
                         $(basename -a "%{buildroot}%{daos_root}/lib64/libdaos_self_test"*) \
                         $(basename -a "%{buildroot}%{daos_root}/lib64/libdfuse"*) \
                         $(basename -a "%{buildroot}%{daos_root}/lib64/libioil"*) \
                         $(basename -a "%{buildroot}%{daos_root}/lib64/libpil4dfs"*) \
                         $(basename -a "%{buildroot}%{daos_root}/lib64/libdaos_common."*)
utils/rpms/move_files.sh "%{buildroot}" "%{buildroot}%{daos_root}/include" \
                         "%{buildroot}%{_includedir}" \
                         "%{buildroot}%{daos_root}" \
                         "%{buildroot}%{_prefix}" "lib64" "%{_libdir}" \
                         $(basename -a "%{buildroot}%{daos_root}/include/"*)
utils/rpms/move_files.sh "%{buildroot}" "%{buildroot}%{daos_root}/lib/daos/python" \
                         "%{buildroot}%{_prefix}/lib/daos/python" \
                         "%{buildroot}%{daos_root}" \
                         "%{buildroot}%{_prefix}" "lib64" "%{_libdir}" \
                         $(basename -a "%{buildroot}%{daos_root}/lib/daos/python/"*)
mkdir -p "%{buildroot}%{python3_sitearch}/pydaos"
utils/rpms/move_files.sh "%{buildroot}" \
                "%{buildroot}"$(sed "s!%{_prefix}!%{daos_root}!" <<< "%{python3_sitearch}/pydaos") \
                "%{buildroot}%{python3_sitearch}/pydaos" "%{buildroot}%{daos_root}" \
                "%{buildroot}%{_prefix}" "lib64" "%{_libdir}" \
                $(basename -a "%{buildroot}%{daos_root}/lib64/python"*"/site-packages/pydaos/"*)
utils/rpms/move_files.sh "%{buildroot}" "%{buildroot}%{daos_root}/share/man" \
                         "%{buildroot}%{_mandir}" \
                         "%{buildroot}%{daos_root}" \
                         "%{buildroot}%{_prefix}" "lib64" "%{_libdir}" \
                         $(basename -a "%{buildroot}%{daos_root}/share/man/"*)
mkdir -p %{buildroot}/%{conf_dir}
utils/rpms/move_files.sh "%{buildroot}" "%{buildroot}%{daos_root}/etc" \
                         "%{buildroot}%{conf_dir}" \
                         "%{buildroot}%{daos_root}" \
                         "%{buildroot}%{_prefix}" "lib64" "%{_libdir}" \
                         $(basename -a "%{buildroot}%{daos_root}/etc/"*.yml) \
                         $(basename -a "%{buildroot}%{daos_root}/etc/"*.yaml) \
                         $(basename -a "%{buildroot}%{daos_root}/etc/"*.supp)
utils/rpms/fix_files.sh "%{buildroot}" "%{buildroot}%{daos_root}"
ln -s /opt/daos/daos_server %{buildroot}%{_bindir}/daos_server
ln -s /opt/daos/daos_server_helper %{buildroot}%{_bindir}/daos_server_helper
ln -s /opt/daos/daos_firmware_helper %{buildroot}%{_bindir}/daos_firmware_helper
ln -s /opt/daos/daos_engine %{buildroot}%{_bindir}/daos_engine
ln -s /opt/daos/ddb %{buildroot}%{_bindir}/ddb
ln -s /opt/daos/daos_metrics %{buildroot}%{_bindir}/daos_metrics
ln -s %{daos_root}/share/%{name} %{buildroot}%{_datadir}/%{name}
%if ("%{?compiler_args}" == "COMPILER=covc")
mv test.cov %{buildroot}%{daos_root}/lib/daos/TESTING/ftest/test.cov
%endif
%if %{with server}
mkdir -p %{buildroot}/%{_sysctldir}
install -m 644 utils/rpms/%{sysctl_script_name} %{buildroot}/%{_sysctldir}
%endif
mkdir -p %{buildroot}/%{_unitdir}
%if %{with server}
install -m 644 utils/systemd/%{server_svc_name} %{buildroot}/%{_unitdir}
%endif
install -m 644 utils/systemd/%{agent_svc_name} %{buildroot}/%{_unitdir}
mkdir -p %{buildroot}/%{conf_dir}/certs/clients
mkdir -p  %{buildroot}%{_sysconfdir}/bash_completion.d
mv %{buildroot}%{daos_root}/etc/bash_completion.d/* %{buildroot}/%{_sysconfdir}/bash_completion.d
# fixup env-script-interpreters
sed -i -e '1s/env //' %{buildroot}%{daos_root}/lib/daos/TESTING/ftest/{cart/cart_logtest,cart/daos_sys_logscan,config_file_gen,launch,slurm_setup,tags,verify_perms}.py
%if %{with server}
sed -i -e '1s/env //' %{buildroot}%{daos_root}/bin/daos_storage_estimator.py
%endif

# shouldn't have source files in a non-devel RPM
rm -f %{buildroot}%{daos_root}/lib/daos/TESTING/ftest/cart/{test_linkage.cpp,utest_{hlc,portnumber,protocol,swim}.c,wrap_cmocka.h}

%if %{with server}
%pre server
getent group daos_metrics >/dev/null || groupadd -r daos_metrics
getent group daos_server >/dev/null || groupadd -r daos_server
getent group daos_daemons >/dev/null || groupadd -r daos_daemons
getent passwd daos_server >/dev/null || useradd -s /sbin/nologin -r -g daos_server -G daos_metrics,daos_daemons daos_server

%post server
%{?run_ldconfig}
%systemd_post %{server_svc_name}
%sysctl_apply %{sysctl_script_name}

%preun server
%systemd_preun %{server_svc_name}

# all of these macros are empty on EL so keep rpmlint happy
%if (0%{?suse_version} > 0)
%postun server
%{?run_ldconfig}
%systemd_postun %{server_svc_name}
%endif
%endif

%pre client
getent group daos_agent >/dev/null || groupadd -r daos_agent
getent group daos_daemons >/dev/null || groupadd -r daos_daemons
getent passwd daos_agent >/dev/null || useradd -s /sbin/nologin -r -g daos_agent -G daos_daemons daos_agent

%post client
%systemd_post %{agent_svc_name}

%preun client
%systemd_preun %{agent_svc_name}

%if (0%{?suse_version} > 0)
%postun client
%systemd_postun %{agent_svc_name}
%endif

%files
%defattr(-, root, root, -)
%doc README.md
%dir %attr(0755,root,root) %{conf_dir}/certs
%config(noreplace) %{conf_dir}/memcheck-cart.supp
%dir %{conf_dir}
%dir %{_sysconfdir}/bash_completion.d
%{_sysconfdir}/bash_completion.d/daos.bash
# Certificate generation files
%dir %{_libdir}/%{name}
%{_libdir}/%{name}/certgen/
%{_libdir}/%{name}/VERSION
%{_libdir}/libcart.so.*
%{_libdir}/libgurt.so.*
%{_libdir}/libdaos_common.so

%files %{libfabric_name}
%defattr(-,root,root,-)
%license deps/ofi/COPYING
%doc deps/ofi/NEWS.md
%{_bindir}/fi_info
%{_bindir}/fi_pingpong
%{_bindir}/fi_strerror
%if 0%{?rhel}
%{_libdir}/libfabric*.so.1*
%endif
%{_mandir}/man1/fi_*.1*

%if 0%{?suse_version}
%files -n %{libfabirc_name}
%defattr(-,root,root)
%{_libdir}/libfabric*.so.1*
%endif

%files %{libfabric_name}-devel
%defattr(-,root,root)
%license deps/ofi/COPYING
%doc deps/ofi/AUTHORS deps/ofi/README
# We knowingly share this with kernel-headers and librdmacm-devel
# https://github.com/ofiwg/libfabric/issues/1277
%{_includedir}/rdma/
%{_libdir}/libfabric*.so
%{_libdir}/pkgconfig/%{libfabric_name}.pc
%{_mandir}/man3/fi_*.3*
%{_mandir}/man7/fi_*.7*

%files mercury
%license deps/mercury/LICENSE.txt
%doc deps/mercury/Documentation/CHANGES.md
%{_bindir}/hg_*
%{_bindir}/na_*
%{_libdir}/libmercury*.so.*
%{_libdir}/libna*.so.*
%{_libdir}/mercury/libna_plugin_ofi.so

%if %{with ucx}
%files mercury-ucx
%{_libdir}/mercury/libna_plugin_ucx.so
%endif

%files mercury-devel
%license deps/mercury/LICENSE.txt
%doc deps/mercury/README.md
%{_includedir}/mercury*
%{_includedir}/na_*
%{_includedir}/boost
%{_libdir}/libmercury.so
%{_libdir}/libmercury_util.so
%{_libdir}/libna.so
%{_libdir}/pkgconfig/mercury*.pc
%{_libdir}/pkgconfig/na*.pc
%{_libdir}/cmake/*

%if %{with server}
%files server
%doc README.md
%config(noreplace) %attr(0644,root,root) %{conf_dir}/daos_server.yml
%dir %attr(0700,daos_server,daos_server) %{conf_dir}/certs/clients
# set daos_server_helper to be setuid root in order to perform privileged tasks
%attr(4750,root,daos_server) %{daos_root}/bin/daos_server_helper
%{_bindir}/daos_server_helper
# set daos_server to be setgid daos_server in order to invoke daos_server_helper
# and/or daos_firmware_helper
%attr(2755,root,daos_server) %{daos_root}/bin/daos_server
%{_bindir}/daos_server
%{daos_root}/bin/daos_engine
%{_bindir}/daos_engine
%{daos_root}/bin/daos_metrics
%{_bindir}/daos_metrics
%{daos_root}/bin/ddb
%{_bindir}/ddb
%dir %{daos_root}/lib64/daos_srv
%{daos_root}/lib64/daos_srv/libchk.so
%{daos_root}/lib64/daos_srv/libcont.so
%{daos_root}/lib64/daos_srv/libddb.so
%{daos_root}/lib64/daos_srv/libdtx.so
%{daos_root}/lib64/daos_srv/libmgmt.so
%{daos_root}/lib64/daos_srv/libobj.so
%{daos_root}/lib64/daos_srv/libpool.so
%{daos_root}/lib64/daos_srv/librdb.so
%{daos_root}/lib64/daos_srv/librdbt.so
%{daos_root}/lib64/daos_srv/librebuild.so
%{daos_root}/lib64/daos_srv/librsvc.so
%{daos_root}/lib64/daos_srv/libsecurity.so
%{daos_root}/lib64/daos_srv/libvos_srv.so
%{daos_root}/lib64/daos_srv/libvos_size.so
%{daos_root}/lib64/daos_srv/libvos.so
%{daos_root}/lib64/daos_srv/libbio.so
%{daos_root}/lib64/daos_srv/libplacement.so
%{daos_root}/lib64/daos_srv/libpipeline.so
%{daos_root}/lib64/libdaos_common_pmem.so
%{daos_root}/lib64/libdav_v2.so
%config(noreplace) %{conf_dir}/vos_size_input.yaml
%{daos_root}/bin/daos_storage_estimator.py
%{daos_root}/lib64/python*/*/storage_estimator/*.py
%{daos_root}/share/%{name}
%{_datarootdir}/%{name}
%{_unitdir}/%{server_svc_name}
%{_sysctldir}/%{sysctl_script_name}
%endif

%files admin
%doc README.md
%{_bindir}/dmg
%{_mandir}/man8/dmg.8*
%config(noreplace) %{conf_dir}/daos_control.yml

%files client
%doc README.md
%{_libdir}/libdaos.so.*
%{_bindir}/cart_ctl
%{_bindir}/self_test
%{_bindir}/daos_agent
%{_bindir}/dfuse
%{_bindir}/daos
%{_libdir}/libdaos_cmd_hdlrs.so
%{_libdir}/libdaos_self_test.so
%{_libdir}/libdfs.so
%{_libdir}/libds3.so
%{_libdir}/%{name}/API_VERSION
%{_libdir}/libduns.so
%{_libdir}/libdfuse.so
%{_libdir}/libioil.so
%{_libdir}/libpil4dfs.so
%dir %{python3_sitearch}/pydaos
%{python3_sitearch}/pydaos/*.py
%dir %{python3_sitearch}/pydaos/raw
%{python3_sitearch}/pydaos/raw/*.py
%dir %{python3_sitearch}/pydaos/torch
%{python3_sitearch}/pydaos/torch/*.py
%if (0%{?rhel} >= 8)
%dir %{python3_sitearch}/pydaos/__pycache__
%{python3_sitearch}/pydaos/__pycache__/*.pyc
%dir %{python3_sitearch}/pydaos/raw/__pycache__
%{python3_sitearch}/pydaos/raw/__pycache__/*.pyc
%dir %{python3_sitearch}/pydaos/torch/__pycache__
%{python3_sitearch}/pydaos/torch/__pycache__/*.pyc
%endif
%{python3_sitearch}/pydaos/pydaos_shim.so
%{python3_sitearch}/pydaos/torch/torch_shim.so
%exclude %{daos_root}/share/%{name}/ioil-ld-opts
%config(noreplace) %{conf_dir}/daos_agent.yml
%{_unitdir}/%{agent_svc_name}
%{_mandir}/man8/daos.8*

%files deps-common
%dir %{daos_root}/prereq/release/argobots
%{daos_root}/prereq/release/argobots/*
%exclude %{daos_root}/prereq/release/fused/*

%files client-tests
%doc README.md
%dir %{daos_root}/lib/%{name}
%{daos_root}/lib/%{name}/TESTING
%exclude %{daos_root}/lib/%{name}/TESTING/ftest/avocado_tests.yaml
%{daos_root}/bin/hello_drpc
%{daos_root}/lib64/libdaos_tests.so
%{daos_root}/bin/acl_dump_test
%{daos_root}/bin/agent_tests
%{daos_root}/bin/drpc_engine_test
%{daos_root}/bin/drpc_test
%{daos_root}/bin/dfuse_test
%{daos_root}/bin/eq_tests
%{daos_root}/bin/job_tests
%{daos_root}/bin/jump_pl_map
%{daos_root}/bin/pl_bench
%{daos_root}/bin/ring_pl_map
%{daos_root}/bin/security_test
%config(noreplace) %{conf_dir}/fault-inject-cart.yaml
%{daos_root}/bin/fault_status
%{daos_root}/bin/crt_launch
%{daos_root}/bin/daos_perf
%{daos_root}/bin/daos_racer
%{daos_root}/bin/daos_test
%{daos_root}/bin/daos_debug_set_params
%{daos_root}/bin/dfs_test
%{daos_root}/bin/jobtest
%{daos_root}/bin/daos_gen_io_conf
%{daos_root}/bin/daos_run_io_conf
%{daos_root}/lib64/libdpar.so

%files client-tests-openmpi
%doc README.md
%{daos_root}/lib64/libdpar_mpi.so

%files client-tests-mpich
%doc README.md

%if %{with server}
%files server-deps-common
%dir %{daos_root}/prereq/release/spdk
%{daos_root}/prereq/release/spdk/*
%dir %{daos_root}/prereq/release/pmdk
%{daos_root}/prereq/release/pmdk/*

%files server-tests
%doc README.md
%{daos_root}/bin/dtx_tests
%{daos_root}/bin/dtx_ut
%{daos_root}/bin/evt_ctl
%{daos_root}/bin/rdbt
%{daos_root}/bin/smd_ut
%{daos_root}/bin/bio_ut
%{daos_root}/bin/vea_ut
%{daos_root}/bin/vos_tests
%{daos_root}/bin/vea_stress
%{daos_root}/bin/ddb_tests
%{daos_root}/bin/ddb_ut
%{daos_root}/bin/obj_ctl
%{daos_root}/bin/vos_perf
%endif

%files devel
%doc README.md
%{_includedir}/*
%{_libdir}/libdaos.so
%{_libdir}/libgurt.so
%{_libdir}/libcart.so
%{_libdir}/*.a
%{daoshome}/python

%if %{with server}
%files firmware
%doc README.md
# set daos_firmware_helper to be setuid root in order to perform privileged tasks
%attr(4750,root,daos_server) %{daos_root}/bin/daos_firmware_helper
%{_bindir}/daos_firmware_helper
%endif

%files serialize
%doc README.md
%{_libdir}/libdaos_serialize.so

%files tests
%doc README.md
# No files in a meta-package

%files tests-internal
%doc README.md
# No files in a meta-package

%files mofed-shim
%doc README.md
# No files in a shim package

%changelog
* Fri Mar 21 2025  Cedric Koch-Hofer <cedric.koch-hofer@intel.com> 2.7.101-8
- Add support of the libasan

* Tue Mar 18 2025 Jeff Olivier  <jeffolivier@google.com> 2.7.101-7
- Remove raft as external dependency

* Mon Mar 10 2025 Jeff Olivier <jeffolivie@google.com> 2.7.101-6
- Remove server from Ubuntu packaging and fix client only build

* Wed Jan 22 2025 Jan Michalski <jan-marian.michalski@hpe.com> 2.7.101-5
- Add ddb_ut and dtx_ut to the server-tests package

* Fri Dec 20 2024 Jeff Olivier <jeffolivier@google.com> 2.7.101-4
- Switch libfuse3 to libfused

* Thu Dec 19 2024 Phillip Henderson <phillip.henderson@intel.com> 2.7.101-3
- Fix protobuf-c requiremnent for daos-client-tests on Leap.

* Thu Nov 14 2024 Denis Barakhtanov <dbarahtanov@enakta.com> 2.7.101-2
- Add pydaos.torch module to daos-client rpm.

* Fri Nov 08 2024 Phillip Henderson <phillip.henderson@intel.com> 2.7.101-1
- Bump version to 2.7.100

* Tue Nov 5 2024 Michael MacDonald <mjmac@google.com> 2.7.100-11
- Move daos_metrics tool to daos package for use on both clients
  and servers.

* Fri Nov 1 2024 Sherin T George <sherin-t.george@hpe.com> 2.7.100-10
- The modified DAV allocator with memory bucket support for md_on_ssd
  phase-2 is delivered as dav_v2.so.

* Tue Oct 15 2024 Brian J. Murrell <brian.murrell@intel.com> - 2.7.100-9
- Drop BRs for UCX as they were obsoleted as of e01970d

* Mon Oct 07 2024 Cedric Koch-Hofer <cedric.koch-hofer@intel.com> 2.7.100-8
- Update BR: argobots to 1.2

* Tue Oct 01 2024 Tomasz Gromadzki <tomasz.gromadzki@intel.com> 2.7.100-7
- Add support of the PMDK package 2.1.0 with NDCTL enabled.
  * Increase the default ULT stack size to 20KiB if the engine uses
    the DCPM storage class.
  * Prevent using the RAM storage class (simulated PMem) when
    the shutdown state (SDS) is active.
    * Automatically disable SDS for the RAM storage class on engine startup.
    * Force explicitly setting the PMEMOBJ_CONF='sds.at_create=0'
      environment variable to deactivate SDS for the DAOS tools
      (ddb, daos_perf, vos_perf, etc.) when used WITHOUT DCPM.
      Otherwise, a user is supposed to be stopped by an error
      like: "Unsafe shutdown count is not supported for this source".

* Mon Sep 23 2024 Kris Jacque <kris.jacque@intel.com> 2.7.100-6
- Bump min supported go version to 1.21

* Thu Aug 15 2024 Michael MacDonald <mjmac@google.com> 2.7.100-5
- Add libdaos_self_test.so to client RPM

* Mon Aug 05 2024 Jerome Soumagne <jerome.soumagne@intel.com> 2.7.100-4
- Bump mercury version to 2.4.0rc4

* Thu Jul 11 2024 Dalton Bohning <dalton.bohning@intel.com> 2.7.100-3
- Add pciutils-devel build dep for client-tests package

* Mon Jun 24 2024 Tom Nabarro <tom.nabarro@intel.com> 2.7.100-2
- Add pciutils runtime dep for daos_server lspci call
- Add pciutils-devel build dep for pciutils CGO bindings

* Mon May 20 2024 Phillip Henderson <phillip.henderson@intel.com> 2.7.100-1
- Bump version to 2.7.100

* Fri May 03 2024 Lei Huang <lei.huang@intel.com> 2.5.101-5
- Add libaio as a dependent package

* Fri Apr 05 2024 Fan Yong <fan.yong@intel.com> 2.5.101-4
- Catastrophic Recovery

* Thu Apr 04 2024 Ashley M. Pittman <ashley.m.pittman@intel.com> 2.5.101-3
- Update pydaos install process
- Add a dependency from daos-client-tests to daos-devel

* Mon Mar 18 2024 Jan Michalski <jan.michalski@intel.com> 2.5.101-2
- Add dtx_tests to the server-tests package

* Fri Mar 15 2024 Phillip Henderson <phillip.henderson@intel.com> 2.5.101-1
- Bump version to 2.5.101

* Tue Feb 27 2024 Li Wei <wei.g.li@intel.com> 2.5.100-16
- Update raft to 0.11.0-1.416.g12dbc15

* Mon Feb 12 2024 Ryon Jensen <ryon.jensen@intel.com> 2.5.100-15
- Updated isa-l package name to match EPEL

* Tue Jan 09 2024 Brian J. Murrell <brian.murrell@intel.com> 2.5.100-14
- Move /etc/ld.so.conf.d/daos.conf to daos-server sub-package

* Wed Dec 06 2023 Brian J. Murrell <brian.murrell@intel.com> 2.5.100-13
- Update for EL 8.8 and Leap 15.5
- Update raft to 0.10.1-2.411.gefa15f4

* Fri Nov 17 2023 Tomasz Gromadzki <tomasz.gromadzki@intel.com> 2.5.100-12
- Update to PMDK 2.0.0
  * Remove libpmemblk from dependencies.
  * Start using BUILD_EXAMPLES=n and BUILD_BENCHMARKS=n instead of patches.
  * Stop using BUILD_RPMEM=n (removed) and NDCTL_DISABLE=y (invalid).
  * Point https://github.com/pmem/pmdk as the main PMDK reference source.
  NOTE: PMDK upgrade to 2.0.0 does not affect any API call used by DAOS.
        libpmemobj (and libpmem) API stays unchanged.

* Wed Nov 15 2023 Jerome Soumagne <jerome.soumagne@intel.com> 2.5.100-11
- Bump mercury min version to 2.3.1

* Fri Nov 03 2023 Phillip Henderson <phillip.henderson@intel.com> 2.5.100-10
- Move verify_perms.py location

* Wed Aug 23 2023 Brian J. Murrell <brian.murrell@intel.com> 2.5.100-9
- Update fuse3 requirement to R: /usr/bin/fusermount3 by path
  rather than by package name, for portability and future-proofing
- Adding fuse3-devel as a requirement for daos-client-tests subpackage

* Tue Aug 08 2023 Brian J. Murrell <brian.murrell@intel.com> 2.5.100-8
- Build on EL9
- Add a client-tests-mpich subpackage for mpich test dependencies.

* Fri Jul 07 2023 Brian J. Murrell <brian.murrell@intel.com> 2.5.100-7
- Fix golang daos-client-tests dependency to be go instead

* Thu Jun 29 2023 Michael MacDonald <mjmac.macdonald@intel.com> 2.5.100-6
- Install golang >= 1.18 as a daos-client-tests dependency

* Thu Jun 22 2023 Li Wei <wei.g.li@intel.com> 2.5.100-5
- Update raft to 0.10.1-1.408.g9524cdb

* Wed Jun 14 2023 Mohamad Chaarawi <mohamad.chaarawi@intel.com> - 2.5.100-4
- Add pipeline lib

* Wed Jun 14 2023 Wang Shilong <shilong.wang@intel.com> 2.5.100-3
- Remove lmdb-devel for MD on SSD

* Wed Jun 07 2023 Ryon Jensen <ryon.jensen@intel.com> 2.5.100-2
- Removed unnecessary test files

* Tue Jun 06 2023 Jeff Olivier <jeffrey.v.olivier@intel.com> 2.5.100-1
- Switch version to 2.5.100 for 2.6 test builds

* Mon Jun  5 2023 Jerome Soumagne <jerome.soumagne@intel.com> 2.3.107-7
- Remove libfabric pinning and allow for 1.18 builds

* Fri May 26 2023 Jeff Olivier <jeffrey.v.olivier@intel.com> 2.3.107-6
- Add lmdb-devel and bio_ut for MD on SSD

* Tue May 23 2023 Lei Huang <lei.huang@intel.com> 2.3.107-5
- Add libcapstone-devel to deps of client-tests package

* Tue May 16 2023 Lei Huang <lei.huang@intel.com> 2.3.107-4
- Add libcapstone as a new prerequisite package
- Add libpil4dfs.so in daos-client rpm

* Mon May 15 2023 Jerome Soumagne <jerome.soumagne@intel.com> 2.3.107-3
- Fix libfabric/libfabric1 dependency mismatch on SuSE

* Wed May 10 2023 Jerome Soumagne <jerome.soumagne@intel.com> 2.3.107-2
- Temporarily pin libfabric to < 1.18

* Fri May 5 2023 Johann Lombardi <johann.lombardi@intel.com> 2.3.107-1
- Bump version to 2.3.107

* Fri Mar 17 2023 Tom Nabarro <tom.nabarro@intel.com> 2.3.106-2
- Add numactl requires for server package

* Tue Mar 14 2023 Brian J. Murrell <brian.murrell@intel.com> 2.3.106-1
- Bump version to be higher than TB5

* Wed Feb 22 2023 Li Wei <wei.g.li@intel.com> 2.3.103-6
- Update raft to 0.9.2-1.403.g3d20556

* Tue Feb 21 2023 Michael MacDonald <mjmac.macdonald@intel.com> 2.3.103-5
- Bump min supported go version to 1.17

* Fri Feb 17 2023 Ashley M. Pittman <ashley.m.pittman@intel.com> 2.3.103-4
- Add protobuf-c-devel to deps of client-tests package

* Mon Feb 13 2023 Brian J. Murrell <brian.murrell@intel.com> 2.3.103-3
- Remove explicit R: protobuf-c and let the auto-dependency generator
  handle it

* Wed Feb 8 2023 Michael Hennecke <michael.hennecke@intel.com> 2.3.103-2
- Change ipmctl requirement from v2 to v3

* Fri Jan 27 2023 Phillip Henderson <phillip.henderson@intel.com> 2.3.103-1
- Bump version to 2.3.103

* Wed Jan 25 2023 Johann Lombardi <johann.lombardi@intel.com> 2.3.102-1
- Bump version to 2.3.102

* Tue Jan 24 2023 Phillip Henderson <phillip.henderson@intel.com> 2.3.101-7
- Fix daos-tests-internal requirement for daos-tests

* Fri Jan 6 2023 Brian J. Murrell <brian.murrell@intel.com> 2.3.101-6
- Don't need to O: cart any more
- Add %%doc to all packages
- _datadir -> _datarootdir
- Don't use PREFIX= with scons in %%build
- Fix up some hard-coded paths to use macros instead
- Use some guards to prevent creating empty scriptlets

* Tue Dec 06 2022 Joseph G. Moore <joseph.moore@intel.com> 2.3.101-5
- Update Mercury to 2.2.0-6

* Thu Dec 01 2022 Tom Nabarro <tom.nabarro@intel.com> 2.3.101-4
- Update SPDK dependency requirement to greater than or equal to 22.01.2.

* Tue Oct 18 2022 Brian J. Murrell <brian.murrell@intel.com> 2.3.101-3
- Set flag to build per-subpackage debuginfo packages for Leap 15

* Thu Oct 6 2022 Michael MacDonald <mjmac.macdonald@intel.com> 2.3.101-2
- Rename daos_admin -> daos_server_helper

* Tue Sep 20 2022 Johann Lombardi <johann.lombardi@intel.com> 2.3.101-1
- Bump version to 2.3.101

* Thu Sep 8 2022 Jeff Olivier <jeffrey.v.olivier@intel.com> 2.3.100-22
- Move io_conf files from bin to TESTING

* Tue Aug 16 2022 Jeff Olivier <jeffrey.v.olivier@intel.com> 2.3.100-21
- Update PMDK to 1.12.1~rc1 to fix DAOS-11151

* Thu Aug 11 2022 Wang Shilong <shilong.wang@intel.com> 2.3.100-20
- Add daos_debug_set_params to daos-client-tests rpm for fault injection test.

* Fri Aug 5 2022 Jerome Soumagne <jerome.soumagne@intel.com> 2.3.100-19
- Update to mercury 2.2.0

* Tue Jul 26 2022 Michael MacDonald <mjmac.macdonald@intel.com> 2.3.100-18
- Bump min supported go version to 1.16

* Mon Jul 18 2022 Jerome Soumagne <jerome.soumagne@intel.com> 2.3.100-17
- Remove now unused openpa dependency

* Fri Jul 15 2022 Jeff Olivier <jeffrey.v.olivier@intel.com> 2.3.100-16
- Add pool_scrubbing_tests to test package

* Wed Jul 13 2022 Tom Nabarro <tom.nabarro@intel.com> 2.3.100-15
- Update SPDK dependency requirement to greater than or equal to 22.01.1.

* Mon Jun 27 2022 Jerome Soumagne <jerome.soumagne@intel.com> 2.3.100-14
- Update to mercury 2.2.0rc6

* Fri Jun 17 2022 Jeff Olivier <jeffrey.v.olivier@intel.com> 2.3.100-13
- Remove libdts.so, replace with build time static

* Thu Jun 2 2022 Jeff Olivier <jeffrey.v.olivier@intel.com> 2.3.100-12
- Make ucx required for build on all platforms

* Wed Jun 1 2022 Michael MacDonald <mjmac.macdonald@intel.com> 2.3.100-11
- Move dmg to new daos-admin RPM

* Wed May 18 2022 Lei Huang <lei.huang@intel.com> 2.3.100-10
- Update to libfabric to v1.15.1-1 to include critical performance patches

* Tue May 17 2022 Phillip Henderson <phillip.henderson@intel.com> 2.3.100-9
- Remove doas-client-tests-openmpi dependency from daos-tests
- Add daos-tests-internal package

* Mon May  9 2022 Ashley Pittman <ashley.m.pittman@intel.com> 2.3.100-8
- Extend dfusedaosbuild test to run in different configurations.

* Fri May  6 2022 Ashley Pittman <ashley.m.pittman@intel.com> 2.3.100-7
- Add dfuse unit-test binary to call from ftest.

* Wed May  4 2022 Joseph Moore <joseph.moore@intel.com> 2.3.100-6
- Update to mercury 2.1.0.rc4-9 to enable non-unified mode in UCX

* Tue Apr 26 2022 Phillip Henderson <phillip.henderson@intel.com> 2.3.100-5
- Move daos_gen_io_conf and daos_run_io_conf to daos-client-tests

* Wed Apr 20 2022 Lei Huang <lei.huang@intel.com> 2.3.100-4
- Update to libfabric to v1.15.0rc3-1 to include critical performance patches

* Tue Apr 12 2022 Li Wei <wei.g.li@intel.com> 2.3.100-3
- Update raft to 0.9.1-1401.gc18bcb8 to fix uninitialized node IDs

* Wed Apr 6 2022 Jeff Olivier <jeffrey.v.olivier@intel.com> 2.3.100-2
- Remove direct MPI dependency from most of tests

* Wed Apr  6 2022 Johann Lombardi <johann.lombardi@intel.com> 2.3.100-1
- Switch version to 2.3.100 for 2.4 test builds

* Wed Apr  6 2022 Joseph Moore <joseph.moore@intel.com> 2.1.100-26
- Add build depends entries for UCX libraries.

* Sat Apr  2 2022 Joseph Moore <joseph.moore@intel.com> 2.1.100-25
- Update to mercury 2.1.0.rc4-8 to include UCX provider patch

* Fri Mar 11 2022 Alexander Oganezov <alexander.a.oganezov@intel.com> 2.1.100-24
- Update to mercury 2.1.0.rc4-6 to include CXI provider patch

* Wed Mar 02 2022 Michael Hennecke <michael.hennecke@intel.com> 2.1.100-23
- DAOS-6344: Create secondary group daos_daemons for daos_server and daos_agent

* Tue Feb 22 2022 Alexander Oganezov <alexander.a.oganezov@intel.com> 2.1.100-22
- Update mercury to include DAOS-9561 workaround

* Sun Feb 13 2022 Michael MacDonald <mjmac.macdonald@intel.com> 2.1.100-21
- Update go toolchain requirements

* Thu Feb 10 2022 Li Wei <wei.g.li@intel.com> 2.1.100-20
- Update raft to 0.9.0-1394.gc81505f to fix membership change bugs

* Wed Jan 19 2022 Michael MacDonald <mjmac.macdonald@intel.com> 2.1.100-19
- Move libdaos_common.so from daos-client to daos package

* Mon Jan 17 2022 Johann Lombardi <johann.lombardi@intel.com> 2.1.100-18
- Update libfabric to 1.14.0 GA and apply fix for DAOS-9376

* Thu Dec 23 2021 Alexander Oganezov <alexander.a.oganezov@intel.com> 2.1.100-17
- Update to v2.1.0-rc4-3 to pick fix for DAOS-9325 high cpu usage
- Change mercury pinning to be >= instead of strict =

* Thu Dec 16 2021 Brian J. Murrell <brian.murrell@intel.com> 2.1.100-16
- Add BR: python-rpm-macros for Leap 15 as python3-base dropped that
  as a R:

* Sat Dec 11 2021 Brian J. Murrell <brian.murrell@intel.com> 2.1.100-15
- Create a shim package to allow daos openmpi packages built with the
  distribution openmpi to install on MOFED systems

* Fri Dec 10 2021 Brian J. Murrell <brian.murrell@intel.com> 2.1.100-14
- Don't make daos-*-tests-openmi a dependency of anything
  - If they are wanted, they should be installed explicitly, due to
    potential conflicts with other MPI stacks

* Wed Dec 08 2021 Alexander Oganezov <alexander.a.oganezov@intel.com> 2.1.100-13
- Remove DAOS-9173 workaround from mercury. Apply DAOS-9173 to ofi

* Tue Dec 07 2021 Alexander Oganezov <alexander.a.oganezov@intel.com> 2.1.100-12
- Apply DAOS-9173 workaround to mercury

* Fri Dec 03 2021 Alexander Oganezov <alexander.a.oganezov@intel.com> 2.1.100-11
- Update mercury to v2.1.0rc4

* Thu Dec 02 2021 Danielle M. Sikich <danielle.sikich@intel.com> 2.1.100-10
- Fix name of daos serialize package

* Sun Nov 28 2021 Tom Nabarro <tom.nabarro@intel.com> 2.1.100-9
- Set rmem_{max,default} sysctl values on server package install to enable
  SPDK pci_event module to operate in unprivileged process (daos_engine).

* Wed Nov 24 2021 Brian J. Murrell <brian.murrell@intel.com> 2.1.100-8
- Remove invalid "%%else if" syntax
- Fix a few other rpmlint warnings

* Tue Nov 16 2021 Wang Shilong <shilong.wang@intel.com> 2.1.100-7
- Update for libdaos major version bump
- Fix version of libpemobj1 for SUSE

* Sat Nov 13 2021 Alexander Oganezov <alexander.a.oganezov@intel.com> 2.1.100-6
- Update OFI to v1.14.0rc3

* Tue Oct 26 2021 Brian J. Murrell <brian.murrell@intel.com> 2.1.100-5
- Create new daos-{client,server}tests-openmpi and daos-server-tests subpackages
- Rename daos-tests daos-client-tests and make daos-tests require all
  other test suites to maintain existing behavior

* Mon Oct 25 2021 Alexander Oganezov <alexander.a.oganezov@intel.com> 2.1.100-4
- Update mercury to v2.1.0rc2

* Wed Oct 20 2021 Jeff Olivier <jeffrey.v.olivier@intel.com> 2.1.100-3
- Explicitly require 1.11.0-3 of PMDK

* Wed Oct 13 2021 David Quigley <david.quigley@intel.com> 2.1.100-2
- Add defusedxml as a required dependency for the test package.

* Wed Oct 13 2021 Johann Lombardi <johann.lombardi@intel.com> 2.1.100-1
- Switch version to 2.1.100 for 2.2 test builds

* Tue Oct 12 2021 Johann Lombardi <johann.lombardi@intel.com> 1.3.106-1
- Version bump to 1.3.106 for 2.0 test build 6

* Fri Oct 8 2021 Alexander Oganezov <alexander.a.oganezov@intel.com> 1.13.105-4
- Update OFI to v1.13.2rc1

* Wed Sep 15 2021 Li Wei <wei.g.li@intel.com> 1.3.105-3
- Update raft to fix InstallSnapshot performance as well as to avoid some
  incorrect 0.8.0 RPMs

* Fri Sep 03 2021 Brian J. Murrell <brian.murrell@intel.com> 1.3.105-2
- Remove R: hwloc; RPM's auto-requires/provides will take care of this

* Tue Aug 24 2021 Jeff Olivier <jeffrey.v.olivier@intel.com> 1.3.105-1
- Version bump to 1.3.105 for 2.0 test build 5

* Mon Aug 09 2021 Yawei <yawei.niu@intel.com> 1.3.104-5
- Fix duplicates
- Add vos_perf

* Thu Aug 05 2021 Christopher Hoffman <christopherx.hoffman@intel.com> 1.3.104-4
- Update conditional statement to include checking for distributions to
  determine which unit files to use for daos-server and daos-agent

* Wed Aug 04 2021 Kris Jacque <kristin.jacque@intel.com> 1.3.104-3
- Move daos_metrics tool from tests package to server package

* Wed Aug 04 2021 Tom Nabarro <tom.nabarro@intel.com> 1.3.104-2
- Update to spdk 21.07 and (indirectly) dpdk 21.05

* Mon Aug 02 2021 Jeff Olivier <jeffrey.v.olivier@intel.com> 1.3.104-1
- Version bump to 1.3.104 for 2.0 test build 4

* Mon Jul 19 2021 Danielle M. Sikich <danielle.sikich@intel.com> 1.3.103-5
- Add DAOS serialization library that requires hdf5

* Wed Jul 14 2021 Li Wei <wei.g.li@intel.com> 1.3.103-4
- Update raft to fix slow leader re-elections

* Tue Jul 13 2021  Maureen Jean <maureen.jean@intel.com> 1.3.103-3
- Add python modules to python3.6 site-packages

* Mon Jul 12 2021 Alexander Oganezov <alexander.a.oganezov@intel.com> 1.3.103-2
- Update to mercury release v2.0.1

* Mon Jul 12 2021 Johann Lombardi <johann.lombardi@intel.com> 1.3.103-1
- Version bump to 1.3.103 for 2.0 test build 3

* Wed Jul 7 2021 Phillip Henderson <phillip.henderson@intel.com> 1.3.102-6
- Update daos-devel to always require the same version daos-client

* Wed Jun 30 2021 Tom Nabarro <tom.nabarro@intel.com> 1.3.102-5
- Update to spdk 21.04 and (indirectly) dpdk 21.05

* Fri Jun 25 2021 Brian J. Murrell <brian.murrell@intel.com> - 1.3.102-4
- Add libuuid-devel back as a requirement of daos-devel

* Wed Jun 23 2021 Li Wei <wei.g.li@intel.com> 1.3.102-3
- Update raft to pick up Pre-Vote

* Mon Jun 14 2021 Jeff Olivier <jeffrey.v.olivier@intel.com> 1.3.102-2
- Update to pmdk 1.11.0-rc1
- Remove dependence on libpmem since we use libpmemobj directly

* Fri Jun 11 2021 Johann Lombardi <johann.lombardi@intel.com> 1.3.102-1
- Version bump to 1.3.102 for 2.0 test build 2

* Wed Jun 02 2021 Johann Lombardi <johann.lombardi@intel.com> 1.3.101-3
- Remove libs from devel package

* Thu May 20 2021 Jeff Olivier <jeffrey.v.olivier@intel.com> 1.3.0-101-2
- Remove client libs from common package

* Wed May 19 2021 Johann Lombardi <johann.lombardi@intel.com> 1.3.101-1
- Version bump to 1.3.101 for 2.0 test build 1

* Fri May 07 2021 Brian J. Murrell <brian.murrell@intel.com> 1.3.0-16
- Enable debuginfo package building on SUSE platforms

* Thu May 06 2021 Brian J. Murrell <brian.murrell@intel.com> 1.3.0-15
- Update to build on EL8

* Wed May 05 2021 Brian J. Murrell <brian.murrell@intel.com> 1.3.0-14
- Package /etc/daos/certs in main/common package so that both server
  and client get it created

* Wed Apr 21 2021 Tom Nabarro <tom.nabarro@intel.com> - 1.3.0-13
- Relax ipmctl version requirement on leap15 as we have runtime checks

* Fri Apr 16 2021 Mohamad Chaarawi <mohamad.chaarawi@intel.com> - 1.3.0-12
- remove dfuse_hl

* Wed Apr 14 2021 Jeff Olivier <jeffrey.v.olivier@intel.com> - 1.3.0-11
- Remove storage_estimator and io_conf from client packages to remove
  any client side dependence on bio and vos (and and PMDK/SPDK)

* Mon Apr 12 2021 Dalton A. Bohning <daltonx.bohning@intel.com> - 1.3.0-10
- Add attr to the test dependencies

* Tue Apr 06 2021 Kris Jacque <kristin.jacque@intel.com> 1.3.0-9
- Add package for daos_firmware helper binary

* Fri Apr 02 2021 Jeff Olivier <jeffrey.v.olivier@intel.com> 1.3.0-8
- Remove unused readline-devel

* Thu Apr 01 2021 Brian J. Murrell <brian.murrell@intel.com> 1.3.0-7
- Update argobots to 1.1

* Tue Mar 30 2021 Maureen Jean <maureen.jean@intel.com> 1.3.0-6
- Change pydaos_shim_3 to pydaos_shim

* Mon Mar 29 2021 Brian J. Murrell <brian.murrell@intel.com> - 1.3.0-5
- Move libdts.so to the daos-tests subpackage

* Tue Mar 23 2021 Alexander Oganezov <alexander.a.oganezov@intel.com> 1.3.0-4
- Update libfabric to v1.12.0
- Disable grdcopy/gdrapi linkage in libfabric


* Thu Mar 18 2021 Maureen Jean <maureen.jean@intel.com> 1.3.0-3
- Update to python3

* Thu Feb 25 2021 Li Wei <wei.g.li@intel.com> 1.3.0-2
- Require raft-devel 0.7.3 that fixes an unstable leadership problem caused by
  removed replicas as well as some Coverity issues

* Wed Feb 24 2021 Brian J. Murrell <brian.murrell@intel.com> - 1.3.0-1
- Version bump up to 1.3.0

* Mon Feb 22 2021 Brian J. Murrell <brian.murrell@intel.com> 1.1.3-3
- Remove all *-devel Requires from daos-devel as none of those are
  actually necessary to build libdaos clients

* Tue Feb 16 2021 Alexander Oganezov <alexander.a.oganezov@intel.com> 1.1.3-2
- Update libfabric to v1.12.0rc1

* Wed Feb 10 2021 Johann Lombardi <johann.lombardi@intel.com> 1.1.3-1
- Version bump up to 1.1.3

* Tue Feb 9 2021 Vish Venkatesan <vishwanath.venkatesan@intel.com> 1.1.2.1-11
- Add new pmem specific version of DAOS common library

* Fri Feb 5 2021 Saurabh Tandan <saurabh.tandan@intel.com> 1.1.2.1-10
- Added dbench as requirement for test package.

* Wed Feb 3 2021 Hua Kuang <hua.kuang@intel.com> 1.1.2.1-9
- Changed License to BSD-2-Clause-Patent

* Wed Feb 03 2021 Brian J. Murrell <brian.murrell@intel.com> - 1.1.2-8
- Update minimum required libfabric to 1.11.1

* Thu Jan 28 2021 Phillip Henderson <phillip.henderson@intel.com> 1.1.2.1-7
- Change ownership and permissions for the /etc/daos/certs directory.

* Sat Jan 23 2021 Alexander Oganezov <alexander.a.oganezov@intel.com> 1.1.2.1-6
- Update to mercury v2.0.1rc1

* Fri Jan 22 2021 Michael MacDonald <mjmac.macdonald@intel.com> 1.1.2.1-5
- Install daos_metrics utility to %%{_bindir}

* Wed Jan 20 2021 Kenneth Cain <kenneth.c.cain@intel.com> 1.1.2.1-4
- Version update for API major version 1, libdaos.so.1 (1.0.0)

* Fri Jan 15 2021 Michael Hennecke <mhennecke@lenovo.com> 1.1.2.1-3
- Harmonize daos_server and daos_agent groups.

* Tue Dec 15 2020 Ashley Pittman <ashley.m.pittman@intel.com> 1.1.2.1-2
- Combine the two memcheck suppressions files.

* Wed Dec 09 2020 Johann Lombardi <johann.lombardi@intel.com> 1.1.2.1-1
- Version bump up to 1.1.2.1

* Fri Dec 04 2020 Li Wei <wei.g.li@intel.com> 1.1.2-3
- Require raft-devel 0.7.1 that fixes recent Coverity issues

* Wed Dec 02 2020 Maureen Jean <maureen.jean@intel.com> - 1.1.2-2
- define scons_args to be BUILD_TYPE=<release|dev>
- the scons default is BUILD_TYPE=release
- BUILD_TYPE=release will disable fault injection in build

* Tue Dec 01 2020 Brian J. Murrell <brian.murrell@intel.com> - 1.1.2-1
- Version bump up to 1.1.2

* Tue Nov 17 2020 Li Wei <wei.g.li@intel.com> 1.1.1-8
- Require raft-devel 0.7.0 that changes log indices and terms to 63-bit

* Wed Nov 11 2020 Tom Nabarro <tom.nabarro@intel.com> 1.1.1-7
- Add version validation for runtime daos_server ipmctl requirement to avoid
  potential corruption of PMMs when setting PMem goal, issue fixed in
  https://github.com/intel/ipmctl/commit/9e3898cb15fa9eed3ef3e9de4488be1681d53ff4

* Thu Oct 29 2020 Jonathan Martinez Montes <jonathan.martinez.montes@intel.com> 1.1.1-6
- Restore obj_ctl utility

* Wed Oct 28 2020 Brian J. Murrell <brian.murrell@intel.com> - 1.1.1-5
- Use %%autosetup
- Only use systemd_requires if it exists
- Obsoletes: cart now that it's included in daos

* Sat Oct 24 2020 Maureen Jean <maureen.jean@intel.com> 1.1.1-4
- Add daos.conf to the daos package to resolve the path to libbio.so

* Tue Oct 13 2020 Jonathan Martinez Montes <jonathan.martinez.montes@intel.com> 1.1.1-3
- Remove obj_ctl from Tests RPM package
- Add libdts.so shared library that is used by daos_perf, daos_racer and
  the daos utility.

* Tue Oct 13 2020 Amanda Justiniano <amanda.justiniano-pagn@intel.com> 1.1.1-3
- Add lbzip2 requirement to the daos-tests package

* Tue Oct 13 2020 Michael MacDonald <mjmac.macdonald@intel.com> 1.1.1-2
- Create unprivileged user for daos_agent

* Mon Oct 12 2020 Johann Lombardi <johann.lombardi@intel.com> 1.1.1-1
- Version bump up to 1.1.1

* Sat Oct 03 2020 Michael MacDonald <mjmac.macdonald@intel.com> 1.1.0-34
- Add go-race to BuildRequires on OpenSUSE Leap

* Wed Sep 16 2020 Alexander Oganezov <alexander.a.oganezov@intel.com> 1.1.0-33
- Update OFI to v1.11.0

* Mon Aug 17 2020 Michael MacDonald <mjmac.macdonald@intel.com> 1.1.0-32
- Install completion script in /etc/bash_completion.d

* Wed Aug 05 2020 Brian J. Murrell <brian.murrell@intel.com> - 1.1.0-31
- Change fuse requirement to fuse3
- Use Lmod for MPI module loading
- Remove unneeded (and un-distro gated) Requires: json-c

* Wed Jul 29 2020 Jonathan Martinez Montes <jonathan.martinez.montes@intel.com> - 1.1.0-30
- Add the daos_storage_estimator.py tool. It merges the functionality of the
  former tools vos_size, vos_size.py, vos_size_dfs_sample.py and parse_csv.py.

* Wed Jul 29 2020 Jeffrey V Olivier <jeffrey.v.olivier@intel.com> - 1.1.0-29
- Revert prior changes from version 28

* Mon Jul 13 2020 Brian J. Murrell <brian.murrell@intel.com> - 1.1.0-28
- Change fuse requirement to fuse3
- Use Lmod for MPI module loading

* Tue Jul 7 2020 Alexander A Oganezov <alexander.a.oganezov@intel.com> - 1.1.0-27
- Update to mercury release 2.0.0~rc1-1

* Sun Jun 28 2020 Jonathan Martinez Montes <jonathan.martinez.montes@intel.com> - 1.1.0-26
- Add the vos_size_dfs_sample.py tool. It is used to generate dynamically
  the vos_dfs_sample.yaml file using the real DFS super block data.

* Tue Jun 23 2020 Jeff Olivier <jeffrey.v.olivier@intel.com> - 1.1.0-25
- Add -no-rpath option and use it for rpm build rather than modifying
  SCons files in place

* Tue Jun 16 2020 Jeff Olivier <jeffrey.v.olivier@intel.com> - 1.1.0-24
- Modify RPATH removal snippet to replace line with pass as some lines
  can't be removed without breaking the code

* Fri Jun 05 2020 Ryon Jensen <ryon.jensen@intel.com> - 1.1.0-23
- Add libisa-l_crypto dependency

* Fri Jun 05 2020 Tom Nabarro <tom.nabarro@intel.com> - 1.1.0-22
- Change server systemd run-as user to daos_server in unit file

* Thu Jun 04 2020 Hua Kuang <hua.kuang@intel.com> - 1.1.0-21
- Remove dmg_old from DAOS RPM package

* Thu May 28 2020 Tom Nabarro <tom.nabarro@intel.com> - 1.1.0-20
- Create daos group to run as in systemd unit file

* Tue May 26 2020 Brian J. Murrell <brian.murrell@intel.com> - 1.1.0-19
- Enable parallel building with _smp_mflags

* Fri May 15 2020 Kenneth Cain <kenneth.c.cain@intel.com> - 1.1.0-18
- Require raft-devel >= 0.6.0 that adds new API raft_election_start()

* Thu May 14 2020 Brian J. Murrell <brian.murrell@intel.com> - 1.1.0-17
- Add cart-devel's Requires to daos-devel as they were forgotten
  during the cart merge

* Thu May 14 2020 Brian J. Murrell <brian.murrell@intel.com> - 1.1.0-16
- Fix fuse3-libs -> libfuse3 for SLES/Leap 15

* Thu Apr 30 2020 Brian J. Murrell <brian.murrell@intel.com> - 1.1.0-15
- Use new properly pre-release tagged mercury RPM

* Thu Apr 30 2020 Brian J. Murrell <brian.murrell@intel.com> - 1.1.0-14
- Move fuse dependencies to the client subpackage

* Mon Apr 27 2020 Michael MacDonald <mjmac.macdonald@intel.com> 1.1.0-13
- Rename /etc/daos.yml -> /etc/daos_control.yml

* Thu Apr 16 2020 Brian J. Murrell <brian.murrell@intel.com> - 1.1.0-12
- Use distro fuse

* Fri Apr 10 2020 Alexander Oganezov <alexander.a.oganezov@intel.com> - 1.1.0-11
- Update to mercury 4871023 to pick na_ofi.c race condition fix for
  "No route to host" errors.

* Sun Apr 05 2020 Brian J. Murrell <brian.murrell@intel.com> - 1.1.0-10
- Clean up spdk dependencies

* Mon Mar 30 2020 Tom Nabarro <tom.nabarro@intel.com> - 1.1.0-9
- Set version of spdk to < v21, > v19

* Fri Mar 27 2020 David Quigley <david.quigley@intel.com> - 1.1.0-8
- add daos and dmg man pages to the daos-client files list

* Thu Mar 26 2020 Michael MacDonald <mjmac.macdonald@intel.com> 1.1.0-7
- Add systemd scriptlets for managing daos_server/daos_agent services

* Thu Mar 26 2020 Alexander Oganeozv <alexander.a.oganezov@intel.com> - 1.1.0-6
- Update ofi to 62f6c937601776dac8a1f97c8bb1b1a6acfbc3c0

* Tue Mar 24 2020 Jeffrey V. Olivier <jeffrey.v.olivier@intel.com> - 1.1.0-5
- Remove cart as an external dependence

* Mon Mar 23 2020 Jeffrey V. Olivier <jeffrey.v.olivier@intel.com> - 1.1.0-4
- Remove scons_local as dependency

* Tue Mar 03 2020 Brian J. Murrell <brian.murrell@intel.com> - 1.1.0-3
- Bump up go minimum version to 1.12

* Thu Feb 20 2020 Brian J. Murrell <brian.murrell@intel.com> - 1.1.0-2
- daos-server requires daos-client (same version)

* Fri Feb 14 2020 Brian J. Murrell <brian.murrell@intel.com> - 1.1.0-1
- Version bump up to 1.1.0

* Wed Feb 12 2020 Brian J. Murrell <brian.murrell@intel.com> - 0.9.0-2
- Remove undefine _missing_build_ids_terminate_build

* Thu Feb 06 2020 Johann Lombardi <johann.lombardi@intel.com> - 0.9.0-1
- Version bump up to 0.9.0

* Sat Jan 18 2020 Jeff Olivier <jeffrey.v.olivier@intel.com> - 0.8.0-3
- Fixing a few warnings in the RPM spec file

* Fri Dec 27 2019 Jeff Olivier <jeffrey.v.olivier@intel.com> - 0.8.0-2
- Remove openmpi, pmix, and hwloc builds, use hwloc and openmpi packages

* Tue Dec 17 2019 Johann Lombardi <johann.lombardi@intel.com> - 0.8.0-1
- Version bump up to 0.8.0

* Thu Dec 05 2019 Johann Lombardi <johann.lombardi@intel.com> - 0.7.0-1
- Version bump up to 0.7.0

* Tue Nov 19 2019 Tom Nabarro <tom.nabarro@intel.com> 0.6.0-15
- Temporarily unconstrain max. version of spdk

* Wed Nov 06 2019 Brian J. Murrell <brian.murrell@intel.com> 0.6.0-14
- Constrain max. version of spdk

* Wed Nov 06 2019 Brian J. Murrell <brian.murrell@intel.com> 0.6.0-13
- Use new cart with R: mercury to < 1.0.1-20 due to incompatibility

* Wed Nov 06 2019 Michael MacDonald <mjmac.macdonald@intel.com> 0.6.0-12
- Add daos_admin privileged helper for daos_server

* Fri Oct 25 2019 Brian J. Murrell <brian.murrell@intel.com> 0.6.0-11
- Handle differences in Leap 15 Python packaging

* Wed Oct 23 2019 Brian J. Murrell <brian.murrell@intel.com> 0.6.0-9
- Update BR: libisal-devel for Leap

* Mon Oct 07 2019 Brian J. Murrell <brian.murrell@intel.com> 0.6.0-8
- Use BR: cart-devel-%%{cart_sha1} if available
- Remove cart's BRs as it's -devel Requires them now

* Tue Oct 01 2019 Brian J. Murrell <brian.murrell@intel.com> 0.6.0-7
- Constrain cart BR to <= 1.0.0

* Sat Sep 21 2019 Brian J. Murrell <brian.murrell@intel.com>
- Remove Requires: {argobots, cart}
  - autodependencies should take care of these

* Thu Sep 19 2019 Jeff Olivier <jeffrey.v.olivier@intel.com>
- Add valgrind-devel requirement for argobots change

* Tue Sep 10 2019 Tom Nabarro <tom.nabarro@intel.com>
- Add requires ndctl as runtime dep for control plane.

* Thu Aug 15 2019 David Quigley <david.quigley@intel.com>
- Add systemd unit files to packaging.

* Thu Jul 25 2019 Brian J. Murrell <brian.murrell@intel.com>
- Add git hash and commit count to release

* Thu Jul 18 2019 David Quigley <david.quigley@intel.com>
- Add certificate generation files to packaging.

* Tue Jul 09 2019 Johann Lombardi <johann.lombardi@intel.com>
- Version bump up to 0.6.0

* Fri Jun 21 2019 David Quigley <dquigley@intel.com>
- Add daos_agent.yml to the list of packaged files

* Thu Jun 13 2019 Brian J. Murrell <brian.murrell@intel.com>
- move obj_ctl daos_gen_io_conf daos_run_io_conf to
  daos-tests sub-package
- daos-server needs spdk-tools

* Fri May 31 2019 Ken Cain <kenneth.c.cain@intel.com>
- Add new daos utility binary

* Wed May 29 2019 Brian J. Murrell <brian.murrell@intel.com>
- Version bump up to 0.5.0
- Add Requires: libpsm_infinipath1 for SLES 12.3

* Tue May 07 2019 Brian J. Murrell <brian.murrell@intel.com>
- Move some files around among the sub-packages

* Mon May 06 2019 Brian J. Murrell <brian.murrell@intel.com>
- Only BR fio
  - fio-{devel,src} is not needed

* Wed Apr 03 2019 Brian J. Murrell <brian.murrell@intel.com>
- initial package
