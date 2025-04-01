%global daos_root /opt/daos

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
%exclude %{_libdir}/*.la

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
%{_mandir}/man7/fabric.7*

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
%dir %{daos_root}/prereq/release/protobufc
%{daos_root}/prereq/release/protobufc/*
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
%exclude %{daos_root}/bin/nvme_control_ctests
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
