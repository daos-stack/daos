%global debug_package %{nil}
%if %{with build_daos}
%if %{with build_deps}
%define deps yes
%else
%define deps no
%endif
%else
%if %{with build_deps}
%define deps only
%endif
%endif

# disable check-buildroot (normally /usr/lib/rpm/check-buildroot) with:
%define __arch_install_post %{nil}

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

# Components using DAOS versioning
%include pmdk.spec
%include fused.spec
%include spdk.spec
# Externally versioned components
%include libfabric.spec
%include mercury.spec
%include isa-l.spec
%include isa-l_crypto.spec
%include argobots.spec

%if %{with build_daos}
%if %{with server}
%package server
Summary: The DAOS server
Version:       %{daos_version}
Release:       %{daos_release}
Requires: %{name}%{?_isa} = %{version}-%{release}
Requires: %{name}-pmdk%{?_isa} = %{version}-%{release}
Requires: %{name}-spdk%{?_isa} = %{version}-%{release}
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
Version:       %{daos_version}
Release:       %{daos_release}
Requires: %{name}%{?_isa} = %{version}-%{release}

%description admin
This package contains DAOS administrative tools (e.g. dmg).

%package client
Summary: The DAOS client
Version:       %{daos_version}
Release:       %{daos_release}
Requires: %{name}%{?_isa} = %{version}-%{release}
Requires: /usr/bin/fusermount3
%{?systemd_requires}

%description client
This is the package needed to run a DAOS client

%package tests
Summary: The entire DAOS test suite
Version:       %{daos_version}
Release:       %{daos_release}
Requires: %{name}-client-tests%{?_isa} = %{version}-%{release}
BuildArch: noarch

%description tests
This is the package is a metapackage to install all of the test packages

%package tests-internal
Summary: The entire internal DAOS test suite
Version:       %{daos_version}
Release:       %{daos_release}
Requires: %{name}-tests = %{version}-%{release}
Requires: %{name}-client-tests-openmpi%{?_isa} = %{version}-%{release}
Requires: %{name}-client-tests-mpich = %{version}-%{release}
Requires: %{name}-serialize%{?_isa} = %{version}-%{release}
BuildArch: noarch

%description tests-internal
This is the package is a metapackage to install all of the internal test
packages

%package client-tests
Summary: The DAOS test suite
Version:       %{daos_version}
Release:       %{daos_release}
Requires: %{name}-client%{?_isa} = %{version}-%{release}
Requires: %{name}-admin%{?_isa} = %{version}-%{release}
Requires: %{name}-devel%{?_isa} = %{version}-%{release}
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
Version:       %{daos_version}
Release:       %{daos_release}
Requires: %{name}-client-tests%{?_isa} = %{version}-%{release}
Requires: hdf5-%{openmpi}-tests
Requires: hdf5-vol-daos-%{openmpi}-tests
Requires: MACSio-%{openmpi}
Requires: simul-%{openmpi}

%description client-tests-openmpi
This is the package needed to run the DAOS client test suite openmpi tools

%package client-tests-mpich
Summary: The DAOS client test suite - tools which need mpich
Version:       %{daos_version}
Release:       %{daos_release}
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
Version:       %{daos_version}
Release:       %{daos_release}
Requires: %{name}-server%{?_isa} = %{version}-%{release}
Requires: %{name}-admin%{?_isa} = %{version}-%{release}

%description server-tests
This is the package needed to run the DAOS server test suite (server tests)
%endif

%package devel
Summary: The DAOS development libraries and headers
Version:       %{daos_version}
Release:       %{daos_release}
Requires: %{name}-client%{?_isa} = %{version}-%{release}
Requires: libuuid-devel

%description devel
This is the package needed to build software with the DAOS library.

%if %{with server}
%package firmware
Summary: The DAOS firmware management helper
Version:       %{daos_version}
Release:       %{daos_release}
Requires: %{name}-server%{?_isa} = %{version}-%{release}

%description firmware
This is the package needed to manage server storage firmware on DAOS servers.
%endif

%package serialize
Summary: DAOS serialization library that uses HDF5
Version:       %{daos_version}
Release:       %{daos_release}
BuildRequires: hdf5-devel
Requires: hdf5

%description serialize
This is the package needed to use the DAOS serialization and deserialization
tools, as well as the preserve option for the filesystem copy tool.

%package mofed-shim
Summary: A shim to bridge MOFED's openmpi to distribution dependency tags
Version:       %{daos_version}
Release:       %{daos_release}
Provides: libmpi.so.40()(64bit)(openmpi-x86_64)
Requires: libmpi.so.40()(64bit)
Provides: libmpi_cxx.so.40()(64bit)(openmpi-x86_64)
Provides: libmpi_cxx.so.40()(64bit)
BuildArch: noarch

%description mofed-shim
This is the package that bridges the difference between the MOFED openmpi
"Provides" and distribution-openmpi consumers "Requires".

%endif

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
      install                         \
      TARGET_TYPE=release               \
      USE_INSTALLED=all               \
      CONF_DIR=%{conf_dir}   \
      --build-deps=%{deps}            \
      PREFIX=%{_prefix}              \
     %{?daos_build_args}            \
      %{?scons_args}                  \
      %{?compiler_args}
utils/rpms/fix_files.sh "%{buildroot}" "%{buildroot}%{_prefix}" "remove-rpath"
%if %{with build_deps}
mkdir -p %{buildroot}/%{_sysconfdir}/bash_completion.d
mv %{buildroot}/%{_prefix}/etc/bash_completion.d/daospmempool %{buildroot}/%{_sysconfdir}/bash_completion.d
%endif
%if %{with build_daos}
%if ("%{?compiler_args}" == "COMPILER=covc")
mv test.cov %{buildroot}%{_prefix}/lib/daos/TESTING/ftest/test.cov
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
mkdir -p %{buildroot}/%{_sysconfdir}/bash_completion.d
mv %{buildroot}/%{conf_dir}/bash_completion.d/* %{buildroot}/%{_sysconfdir}/bash_completion.d
# fixup env-script-interpreters
sed -i -e '1s/env //' %{buildroot}%{_prefix}/lib/daos/TESTING/ftest/{cart/cart_logtest,cart/daos_sys_logscan,config_file_gen,launch,slurm_setup,tags,verify_perms}.py
%if %{with server}
sed -i -e '1s/env //' %{buildroot}%{_prefix}/bin/daos_storage_estimator.py
%endif

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

%if %{with server}
%files server
%doc README.md
%config(noreplace) %attr(0644,root,root) %{conf_dir}/daos_server.yml
%dir %attr(0700,daos_server,daos_server) %{conf_dir}/certs/clients
# set daos_server_helper to be setuid root in order to perform privileged tasks
%attr(4750,root,daos_server) %{_bindir}/daos_server_helper
# set daos_server to be setgid daos_server in order to invoke daos_server_helper
# and/or daos_firmware_helper
%attr(2755,root,daos_server) %{_bindir}/daos_server
%{_bindir}/daos_engine
%{_bindir}/daos_metrics
%{_bindir}/ddb
%dir %{_libdir}/daos_srv
%{_libdir}/daos_srv/libchk.so
%{_libdir}/daos_srv/libcont.so
%{_libdir}/daos_srv/libddb.so
%{_libdir}/daos_srv/libdtx.so
%{_libdir}/daos_srv/libmgmt.so
%{_libdir}/daos_srv/libobj.so
%{_libdir}/daos_srv/libpool.so
%{_libdir}/daos_srv/librdb.so
%{_libdir}/daos_srv/librdbt.so
%{_libdir}/daos_srv/librebuild.so
%{_libdir}/daos_srv/librsvc.so
%{_libdir}/daos_srv/libsecurity.so
%{_libdir}/daos_srv/libvos_srv.so
%{_libdir}/daos_srv/libvos_size.so
%{_libdir}/daos_srv/libvos.so
%{_libdir}/daos_srv/libbio.so
%{_libdir}/daos_srv/libplacement.so
%{_libdir}/daos_srv/libpipeline.so
%{_libdir}/libdaos_common_pmem.so
%{_libdir}/libdav_v2.so
%config(noreplace) %{conf_dir}/vos_size_input.yaml
%{_bindir}/daos_storage_estimator.py
%{_libdir}/python*/*/storage_estimator/*.py
%exclude %{_libdir}/python*/*/storage_estimator/__pycache__
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
%exclude %{_datarootdir}/%{name}/ioil-ld-opts
%config(noreplace) %{conf_dir}/daos_agent.yml
%{_unitdir}/%{agent_svc_name}
%{_mandir}/man8/daos.8*

%files client-tests
%doc README.md
%dir %{_prefix}/lib/%{name}
%{_prefix}/lib/%{name}/TESTING
%exclude %{_prefix}/lib/%{name}/TESTING/ftest/avocado_tests.yaml
%{_bindir}/hello_drpc
%{_libdir}/libdaos_tests.so
%{_bindir}/acl_dump_test
%{_bindir}/agent_tests
%{_bindir}/drpc_engine_test
%{_bindir}/drpc_test
%{_bindir}/dfuse_test
%{_bindir}/eq_tests
%{_bindir}/job_tests
%{_bindir}/jump_pl_map
%{_bindir}/pl_bench
%{_bindir}/ring_pl_map
%{_bindir}/security_test
%config(noreplace) %{conf_dir}/fault-inject-cart.yaml
%{_bindir}/fault_status
%{_bindir}/crt_launch
%{_bindir}/daos_perf
%{_bindir}/daos_racer
%{_bindir}/daos_test
%{_bindir}/daos_debug_set_params
%{_bindir}/dfs_test
%{_bindir}/jobtest
%{_bindir}/daos_gen_io_conf
%{_bindir}/daos_run_io_conf
%{_libdir}/libdpar.so

%files client-tests-openmpi
%doc README.md
%{_libdir}/libdpar_mpi.so

%files client-tests-mpich
%doc README.md

%if %{with server}
%files server-tests
%doc README.md
%{_bindir}/dtx_tests
%{_bindir}/dtx_ut
%{_bindir}/evt_ctl
%{_bindir}/rdbt
%{_bindir}/smd_ut
%{_bindir}/bio_ut
%{_bindir}/vea_ut
%{_bindir}/vos_tests
%{_bindir}/vea_stress
%{_bindir}/ddb_tests
%{_bindir}/ddb_ut
%{_bindir}/obj_ctl
%{_bindir}/vos_perf
%exclude %{_bindir}/nvme_control_ctests
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
%attr(4750,root,daos_server) %{_bindir}/daos_firmware_helper
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
%endif
