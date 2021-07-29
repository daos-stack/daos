%define daoshome %{_exec_prefix}/lib/%{name}
%define server_svc_name daos_server.service
%define agent_svc_name daos_agent.service

%global mercury_version 2.0.1-1%{?dist}
%global libfabric_version 1.12.0
%global __python %{__python3}

%if (0%{?rhel} >= 8)
# https://bugzilla.redhat.com/show_bug.cgi?id=1955184
%define _use_internal_dependency_generator 0
%define __find_requires %{_sourcedir}/bz-1955184_find-requires
%endif

Name:          daos
Version:       1.3.103
Release:       5%{?relval}%{?dist}
Summary:       DAOS Storage Engine

License:       BSD-2-Clause-Patent
URL:           https//github.com/daos-stack/daos
Source0:       %{name}-%{version}.tar.gz
Source1:       bz-1955184_find-requires
%if (0%{?rhel} >= 7)
%if (0%{?rhel} >= 8)
BuildRequires: python3-scons >= 2.4
%else
BuildRequires: python36-scons >= 2.4
%endif
%else
BuildRequires: scons >= 2.4
%endif
BuildRequires: libfabric-devel >= %{libfabric_version}
BuildRequires: mercury-devel = %{mercury_version}
%if (0%{?rhel} < 8) || (0%{?suse_version} > 0)
BuildRequires: openpa-devel
BuildRequires: libpsm2-devel
%endif
BuildRequires: gcc-c++
%if (0%{?rhel} >= 8)
BuildRequires: openmpi-devel
%else
BuildRequires: openmpi3-devel
%endif
BuildRequires: hwloc-devel
%if ("%{?compiler_args}" == "COMPILER=covc")
BuildRequires: bullseye
%endif
%if (0%{?rhel} >= 7)
BuildRequires: argobots-devel >= 1.1
BuildRequires: json-c-devel
%if (0%{?rhel} >= 8)
BuildRequires: boost-python3-devel
%else
BuildRequires: boost-python36-devel
%endif
%else
BuildRequires: libabt-devel >= 1.0rc1
BuildRequires: libjson-c-devel
BuildRequires: boost-devel
%endif
BuildRequires: libpmemobj-devel >= 1.11
%if (0%{?rhel} >= 8)
BuildRequires: fuse3-devel >= 3
%else
BuildRequires: fuse3-devel >= 3.4.2
%endif
%if (0%{?suse_version} >= 1500)
# NB: OpenSUSE is stupid about this... If we just
# specify go >= 1.X, it installs go=1.11 AND 1.X.
BuildRequires: go1.14
BuildRequires: go1.14-race
BuildRequires: libprotobuf-c-devel
BuildRequires: liblz4-devel
%else
BuildRequires: protobuf-c-devel
BuildRequires: lz4-devel
%endif
BuildRequires: spdk-devel >= 21.04
%if (0%{?rhel} >= 7)
BuildRequires: libisa-l-devel
BuildRequires: libisa-l_crypto-devel
%else
BuildRequires: libisal-devel
BuildRequires: libisal_crypto-devel
%endif
BuildRequires: daos-raft-devel = 0.8.0
BuildRequires: openssl-devel
BuildRequires: libevent-devel
BuildRequires: libyaml-devel
BuildRequires: libcmocka-devel
BuildRequires: valgrind-devel
BuildRequires: systemd
%if (0%{?rhel} >= 7)
BuildRequires: numactl-devel
BuildRequires: CUnit-devel
BuildRequires: golang-bin >= 1.12
# needed to retrieve PMM region info through control-plane
BuildRequires: libipmctl-devel
BuildRequires: python36-devel
%if (0%{?rhel} >= 8)
BuildRequires: python3-distro
%else
BuildRequires: python36-distro
%endif
BuildRequires: Lmod
%else
%if (0%{?suse_version} >= 1315)
# see src/client/dfs/SConscript for why we need /etc/os-release
# that code should be rewritten to use the python libraries provided for
# os detection
# prefer over libpsm2-compat
BuildRequires: libpsm_infinipath1
# prefer over libcurl4-mini
BuildRequires: libcurl4
BuildRequires: distribution-release
BuildRequires: libnuma-devel
BuildRequires: cunit-devel
BuildRequires: ipmctl-devel
BuildRequires: python3-devel
BuildRequires: python3-distro
BuildRequires: lua-lmod
BuildRequires: systemd-rpm-macros
%if 0%{?is_opensuse}
%else
# have choice for libcurl.so.4()(64bit) needed by systemd: libcurl4 libcurl4-mini
# have choice for libcurl.so.4()(64bit) needed by cmake: libcurl4 libcurl4-mini
BuildRequires: libcurl4
# have choice for libpsm_infinipath.so.1()(64bit) needed by libfabric1: libpsm2-compat libpsm_infinipath1
# have choice for libpsm_infinipath.so.1()(64bit) needed by openmpi-libs: libpsm2-compat libpsm_infinipath1
BuildRequires: libpsm_infinipath1
%endif
%endif
%endif
Requires: protobuf-c
Requires: openssl
# This should only be temporary until we can get a stable upstream release
# of mercury, at which time the autoprov shared library version should
# suffice
Requires: mercury = %{mercury_version}

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

%package server
Summary: The DAOS server
Requires: %{name}%{?_isa} = %{version}-%{release}
Requires: spdk-tools >= 21.04
Requires: ndctl
# needed to set PMem configuration goals in BIOS through control-plane
%if (0%{?suse_version} >= 1500)
Requires: ipmctl >= 02.00.00.3733
Requires: libpmemobj1 >= 1.11
%else
Requires: ipmctl > 02.00.00.3816
Requires: libpmemobj >= 1.11
%endif
Requires: hwloc
Requires: mercury = %{mercury_version}
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
Requires: libfabric >= %{libfabric_version}
%{?systemd_requires}
Obsoletes: cart < 1000

%description server
This is the package needed to run a DAOS server

%package client
Summary: The DAOS client
Requires: %{name}%{?_isa} = %{version}-%{release}
Requires: mercury = %{mercury_version}
Requires: libfabric >= %{libfabric_version}
%if (0%{?rhel} >= 8)
Requires: fuse3 >= 3
%else
Requires: fuse3 >= 3.4.2
%endif
Obsoletes: cart < 1000
%if (0%{?suse_version} >= 1500)
Requires: libfuse3-3 >= 3.4.2
%else
# because our repo has a deprecated fuse-3.x RPM, make sure we don't
# get it when fuse3 Requires: /etc/fuse.conf
%if (0%{?rhel} >= 8)
Requires: fuse3 >= 3
%else
Requires: fuse < 3, fuse3-libs >= 3.4.2
%endif
%endif
%{?systemd_requires}

%description client
This is the package needed to run a DAOS client

%package tests
Summary: The DAOS test suite
#This is a bit messy and needs some cleanup.  In theory,
#we should have client tests and server tests in separate
#packages but some binaries need libraries from both at
#present.
Requires: %{name}-client%{?_isa} = %{version}-%{release}
Requires: %{name}-server%{?_isa} = %{version}-%{release}
%if (0%{?rhel} >= 7) && (0%{?rhel} < 8)
Requires: python36-distro
Requires: python36-tabulate
%else
Requires: python3-distro
Requires: python3-tabulate
%endif
Requires: fio
Requires: dbench
Requires: lbzip2
Requires: attr
%if (0%{?suse_version} >= 1315)
Requires: libpsm_infinipath1
%endif

%description tests
This is the package needed to run the DAOS test suite

%package devel
Summary: The DAOS development libraries and headers
Requires: %{name}-client%{?_isa} = %{version}-%{release}
Requires: libuuid-devel

%description devel
This is the package needed to build software with the DAOS library.

%package firmware
Summary: The DAOS firmware management helper
Requires: %{name}-server%{?_isa} = %{version}-%{release}

%description firmware
This is the package needed to manage server storage firmware on DAOS servers.

%package daos_serialize
Summary: DAOS serialization library that uses HDF5
BuildRequires: hdf5-devel
Requires: hdf5

%description daos_serialize
This is the package needed to use the DAOS serialization and deserialization
tools, as well as the preserve option for the filesystem copy tool.

%if (0%{?suse_version} > 0)
%global __debug_package 1
%global _debuginfo_subpackages 0
%debug_package
%endif

%prep
%autosetup

%build

%define conf_dir %{_sysconfdir}/daos
%if (0%{?rhel} >= 7)
%define scons_exe scons-3
%else
%define scons_exe scons
%endif
%{scons_exe} %{?_smp_mflags} \
      --config=force         \
      --no-rpath             \
      USE_INSTALLED=all      \
      FIRMWARE_MGMT=yes      \
      CONF_DIR=%{conf_dir}   \
      PREFIX=%{buildroot}    \
     %{?scons_args}          \
     %{?compiler_args}

%if ("%{?compiler_args}" == "COMPILER=covc")
mv test.cov{,-build}
%endif

%install
%{scons_exe} %{?_smp_mflags}          \
      --config=force                  \
      --no-rpath                      \
      --install-sandbox=%{buildroot}  \
      %{buildroot}%{_prefix}          \
      %{buildroot}%{conf_dir}         \
      USE_INSTALLED=all               \
      FIRMWARE_MGMT=yes               \
      CONF_DIR=%{conf_dir}            \
      PREFIX=%{_prefix}               \
      %{?scons_args}                  \
      %{?compiler_args}

%if ("%{?compiler_args}" == "COMPILER=covc")
mv test.cov-build %{buildroot}/usr/lib/daos/TESTING/ftest/test.cov
%endif
mkdir -p %{buildroot}/%{_sysconfdir}/ld.so.conf.d/
echo "%{_libdir}/daos_srv" > %{buildroot}/%{_sysconfdir}/ld.so.conf.d/daos.conf
mkdir -p %{buildroot}/%{_unitdir}
%if (0%{?rhel} == 7)
install -m 644 utils/systemd/%{server_svc_name}.pre230 %{buildroot}/%{_unitdir}/%{server_svc_name}
install -m 644 utils/systemd/%{agent_svc_name}.pre230 %{buildroot}/%{_unitdir}/%{agent_svc_name}
%else
install -m 644 utils/systemd/%{server_svc_name} %{buildroot}/%{_unitdir}
install -m 644 utils/systemd/%{agent_svc_name} %{buildroot}/%{_unitdir}
%endif
mkdir -p %{buildroot}/%{conf_dir}/certs/clients
mv %{buildroot}/%{conf_dir}/bash_completion.d %{buildroot}/%{_sysconfdir}

%pre server
getent group daos_metrics >/dev/null || groupadd -r daos_metrics
getent group daos_server >/dev/null || groupadd -r daos_server
getent passwd daos_server >/dev/null || useradd -s /sbin/nologin -r -g daos_server -G daos_metrics daos_server
%post server
/sbin/ldconfig
%systemd_post %{server_svc_name}
%preun server
%systemd_preun %{server_svc_name}
%postun server
/sbin/ldconfig
%systemd_postun %{server_svc_name}

%pre client
getent group daos_agent >/dev/null || groupadd -r daos_agent
getent passwd daos_agent >/dev/null || useradd -s /sbin/nologin -r -g daos_agent daos_agent
%post client
%systemd_post %{agent_svc_name}
%preun client
%systemd_preun %{agent_svc_name}
%postun client
%systemd_postun %{agent_svc_name}

%files
%defattr(-, root, root, -)
%{_sysconfdir}/ld.so.conf.d/daos.conf
%dir %attr(0755,root,root) %{conf_dir}/certs
%{conf_dir}/memcheck-cart.supp
%dir %{conf_dir}
%dir %{_sysconfdir}/bash_completion.d
%{_sysconfdir}/bash_completion.d/daos.bash
# Certificate generation files
%dir %{_libdir}/%{name}
%{_libdir}/%{name}/certgen/
%{_libdir}/%{name}/VERSION
%{_libdir}/libcart.so.*
%{_libdir}/libgurt.so.*
%doc

%files server
%config(noreplace) %attr(0644,root,root) %{conf_dir}/daos_server.yml
%dir %attr(0700,daos_server,daos_server) %{conf_dir}/certs/clients
# set daos_admin to be setuid root in order to perform privileged tasks
%attr(4750,root,daos_server) %{_bindir}/daos_admin
# set daos_server to be setgid daos_server in order to invoke daos_admin
# and/or daos_firmware
%attr(2755,root,daos_server) %{_bindir}/daos_server
%{_bindir}/daos_engine
%dir %{_libdir}/daos_srv
%{_libdir}/daos_srv/libcont.so
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
%{_libdir}/libdaos_common_pmem.so
%{conf_dir}/vos_size_input.yaml
%{_bindir}/daos_storage_estimator.py
%{python3_sitearch}/storage_estimator/*.py
%dir %{python3_sitearch}/storage_estimator
%if (0%{?rhel} >= 7)
%dir %{python3_sitearch}/storage_estimator/__pycache__
%{python3_sitearch}/storage_estimator/__pycache__/*.pyc
%endif
%{_datadir}/%{name}
%exclude %{_datadir}/%{name}/ioil-ld-opts
%{_unitdir}/%{server_svc_name}

%files client
%{_libdir}/libdaos_common.so
%{_libdir}/libdaos.so.*
%{_bindir}/cart_ctl
%{_bindir}/self_test
%{_bindir}/dmg
%{_bindir}/daos_agent
%{_bindir}/dfuse
%{_bindir}/daos
%{_bindir}/daos_old
%{_libdir}/libdaos_cmd_hdlrs.so
%{_libdir}/libdfs.so
%{_libdir}/%{name}/API_VERSION
%{_libdir}/libduns.so
%{_libdir}/libdfuse.so
%{_libdir}/libioil.so
%{_libdir}/libdfs_internal.so
%dir %{python3_sitearch}/pydaos
%{python3_sitearch}/pydaos/*.py
%dir %{python3_sitearch}/pydaos/raw
%{python3_sitearch}/pydaos/raw/*.py
%if (0%{?rhel} >= 7)
%dir %{python3_sitearch}/pydaos/__pycache__
%{python3_sitearch}/pydaos/__pycache__/*.pyc
%dir %{python3_sitearch}/pydaos/raw/__pycache__
%{python3_sitearch}/pydaos/raw/__pycache__/*.pyc
%endif
%{python3_sitearch}/pydaos/pydaos_shim.so
%{_datadir}/%{name}/ioil-ld-opts
%config(noreplace) %{conf_dir}/daos_agent.yml
%config(noreplace) %{conf_dir}/daos_control.yml
%{_unitdir}/%{agent_svc_name}
%{_mandir}/man8/daos.8*
%{_mandir}/man8/dmg.8*

%files tests
%dir %{_prefix}/lib/daos
%{_prefix}/lib/daos/TESTING
%{_prefix}/lib/daos/TESTING/ftest/list_tests.py
%{_bindir}/hello_drpc
%{_bindir}/*_test*
%{_bindir}/jobtest
%{_bindir}/self_test
%{_libdir}/libdaos_tests.so
%{_bindir}/jump_pl_map
%{_bindir}/ring_pl_map
%{_bindir}/pl_bench
%{_bindir}/smd_ut
%{_bindir}/vea_ut
%{_bindir}/daos_perf
%{_bindir}/daos_racer
%{_bindir}/evt_ctl
%{_bindir}/io_conf
%{_bindir}/rdbt
%{_bindir}/obj_ctl
%{_bindir}/daos_gen_io_conf
%{_bindir}/daos_run_io_conf
%{_bindir}/crt_launch
%{_bindir}/daos_metrics
%{_bindir}/daos_test
%{_bindir}/dfs_test
%{conf_dir}/fault-inject-cart.yaml
%{_bindir}/fault_status
# For avocado tests
%{_prefix}/lib/daos/.build_vars.json
%{_prefix}/lib/daos/.build_vars.sh
%{_libdir}/libdts.so

%files devel
%{_includedir}/*
%{_libdir}/libdaos.so
%{_libdir}/libgurt.so
%{_libdir}/libcart.so
%{_libdir}/*.a

%files firmware
# set daos_firmware to be setuid root in order to perform privileged tasks
%attr(4750,root,daos_server) %{_bindir}/daos_firmware

%files daos_serialize
%{_libdir}/libdaos_serialize.so

%changelog
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
- Install daos_metrics utility to %{_bindir}

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
- Add systemd scriptlets for managing daos_server/daos_admin services

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
- Use BR: cart-devel-%{cart_sha1} if available
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
