%define daoshome %{_exec_prefix}/lib/%{name}

%global spdk_version 19.04.1
%global cart_version 4.7.0

Name:          daos
Version:       0.9.4
Release:       2%{?relval}%{?dist}
Summary:       DAOS Storage Engine

License:       Apache
URL:           https//github.com/daos-stack/daos
Source0:       %{name}-%{version}.tar.gz
Source1:       scons_local-%{version}.tar.gz

BuildRequires: scons
BuildRequires: gcc-c++
BuildRequires: cart-devel >= %{cart_version}
BuildRequires: openmpi3-devel
BuildRequires: hwloc-devel
BuildRequires: libpsm2-devel
%if (0%{?rhel} >= 7)
BuildRequires: argobots-devel >= 1.0rc1
%else
BuildRequires: libabt-devel >= 1.0rc1
%endif
BuildRequires: libpmem-devel, libpmemobj-devel
BuildRequires: fuse3-devel >= 3.4.2
BuildRequires: protobuf-c-devel
BuildRequires: spdk-devel = %{spdk_version}
%if (0%{?rhel} >= 7)
BuildRequires: libisa-l-devel
%else
BuildRequires: libisal-devel
%endif
BuildRequires: raft-devel <= 0.5.0
BuildRequires: hwloc-devel
BuildRequires: openssl-devel
BuildRequires: libevent-devel
BuildRequires: libyaml-devel
BuildRequires: libcmocka-devel
BuildRequires: readline-devel
BuildRequires: valgrind-devel
BuildRequires: systemd
%if (0%{?rhel} >= 7)
BuildRequires: numactl-devel
BuildRequires: CUnit-devel
BuildRequires: golang-bin >= 1.12
BuildRequires: libipmctl-devel
BuildRequires: python-devel python36-devel
BuildRequires: python-distro
%else
%if (0%{?suse_version} >= 1315)
# see src/client/dfs/SConscript for why we need /etc/os-release
# that code should be rewritten to use the python libraries provided for
# os detection
BuildRequires: distribution-release
BuildRequires: libnuma-devel
BuildRequires: cunit-devel
BuildRequires: go >= 1.12
BuildRequires: ipmctl-devel
BuildRequires: python-devel python3-devel
BuildRequires: Modules
BuildRequires: python3-distro
%if 0%{?is_opensuse}
# have choice for boost-devel needed by cart-devel: boost-devel boost_1_58_0-devel
BuildRequires: boost-devel
%else
# have choice for libcurl.so.4()(64bit) needed by systemd: libcurl4 libcurl4-mini
# have choice for libcurl.so.4()(64bit) needed by cmake: libcurl4 libcurl4-mini
BuildRequires: libcurl4
%endif # 0%{?is_opensuse}
%endif # (0%{?suse_version} >= 1315)
%endif # (0%{?rhel} >= 7)
%if (0%{?suse_version} >= 1500)
Requires: libpmem1, libpmemobj1
%endif
Requires: protobuf-c
Requires: spdk >= %{spdk_version}, spdk < 20
Requires: openssl
# this should be satisfied by autoprovides but daos-1.x also provides
# lib{car,gur}t.so.x so if it is in an available repo, RPM will try to
# install it to satisfy the shared libraries.
Requires: cart
# the main/common package needs the client package
Requires: %{name}-client = %{version}-%{release}


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
Requires: %{name} = %{version}-%{release}
Requires: %{name}-client = %{version}-%{release}
Requires: spdk-tools >= %{spdk_version}, spdk-tools < 20
Requires: ndctl
Requires: ipmctl
Requires: hwloc
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description server
This is the package needed to run a DAOS server

%package client
Summary: The DAOS client
Requires: %{name} = %{version}-%{release}
Requires: fuse3 >= 3.4.2
# because our repo has a deprecated fuse-3.x RPM, make sure we don't
# get it when fuse3 Requires: /etc/fuse.conf
Requires: fuse < 3, fuse3-libs >= 3.4.2

%description client
This is the package needed to run a DAOS client

%package tests
Summary: The DAOS test suite
Requires: %{name}-client = %{version}-%{release}
Requires: python-pathlib
%if (0%{?suse_version} >= 1315)
Requires: libpsm_infinipath1
%endif
Requires: fio


%description tests
This is the package needed to run the DAOS test suite

%package devel
# Leap 15 doesn't seem to be creating dependencies as richly as EL7
# for example, EL7 automatically adds:
# Requires: libdaos.so.0()(64bit)
%if (0%{?suse_version} >= 1500)
Requires: %{name}-client = %{version}-%{release}
Requires: %{name} = %{version}-%{release}
%endif
Summary: The DAOS development libraries and headers
Requires: cart-devel >= %{cart_version}

%description devel
This is the package needed to build software with the DAOS library.

%prep
%setup -q
%setup -q -a 1


%build
# remove rpathing from the build
rpath_files="utils/daos_build.py"
rpath_files+=" $(find . -name SConscript)"
sed -i -e '/AppendUnique(RPATH=.*)/d' $rpath_files

%define conf_dir %{_sysconfdir}/daos

scons %{?no_smp_mflags}    \
      --config=force       \
      USE_INSTALLED=all    \
      CONF_DIR=%{conf_dir} \
      PREFIX=%{?buildroot}

%install
scons %{?no_smp_mflags}               \
      --config=force                  \
      --install-sandbox=%{?buildroot} \
      %{?buildroot}%{_prefix}         \
      %{?buildroot}%{conf_dir}        \
      USE_INSTALLED=all               \
      CONF_DIR=%{conf_dir}            \
      PREFIX=%{_prefix}
BUILDROOT="%{?buildroot}"
PREFIX="%{?_prefix}"
mkdir -p %{?buildroot}/%{_sysconfdir}/ld.so.conf.d/
echo "%{_libdir}/daos_srv" > %{?buildroot}/%{_sysconfdir}/ld.so.conf.d/daos.conf
mkdir -p %{?buildroot}/%{_unitdir}
install -m 644 utils/systemd/daos_server.service %{?buildroot}/%{_unitdir}
install -m 644 utils/systemd/daos_agent.service %{?buildroot}/%{_unitdir}

%pre server
getent group daos_admins >/dev/null || groupadd -r daos_admins
%post server -p /sbin/ldconfig
%postun server -p /sbin/ldconfig

%files
%defattr(-, root, root, -)
# you might think libvio.so goes in the server RPM but
# the 2 tools following it need it
%{_libdir}/daos_srv/libbio.so
# you might think libdaos_tests.so goes in the tests RPM but
# the 4 tools following it need it
%{_libdir}/libdaos_tests.so
%{_bindir}/vos_size
%{_bindir}/io_conf
%{_bindir}/jump_pl_map
%{_bindir}/ring_pl_map
%{_bindir}/pl_bench
%{_bindir}/rdbt
%{_bindir}/vos_size.py
%{_libdir}/libvos.so
%dir %{_prefix}%{_sysconfdir}
%{_prefix}%{_sysconfdir}/vos_dfs_sample.yaml
%{_prefix}%{_sysconfdir}/vos_size_input.yaml
%{_libdir}/libdaos_common.so
# TODO: this should move from daos_srv to daos
%{_libdir}/daos_srv/libplacement.so
# Certificate generation files
%dir %{_libdir}/%{name}
%{_libdir}/%{name}/certgen/
%{_libdir}/%{name}/VERSION
%doc

%files server
%config(noreplace) %{conf_dir}/daos_server.yml
%{_sysconfdir}/ld.so.conf.d/daos.conf
# set daos_admin to be setuid root in order to perform privileged tasks
%attr(4750,root,daos_admins) %{_bindir}/daos_admin
# set daos_server to be setgid daos_admins in order to invoke daos_admin
%attr(2755,root,daos_admins) %{_bindir}/daos_server
%{_bindir}/daos_io_server
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
%{_datadir}/%{name}
%exclude %{_datadir}/%{name}/ioil-ld-opts
%{_unitdir}/daos_server.service

%files client
%{_prefix}/etc/memcheck-daos-client.supp
%{_bindir}/dmg
%{_bindir}/dmg_old
%{_bindir}/daosctl
%{_bindir}/dcont
%{_bindir}/daos_agent
%{_bindir}/dfuse
%{_bindir}/daos
%{_bindir}/dfuse_hl
%{_libdir}/*.so.*
%{_libdir}/libdfs.so
%{_libdir}/%{name}/API_VERSION
%{_libdir}/libduns.so
%{_libdir}/libdfuse.so
%{_libdir}/libioil.so
%dir  %{_libdir}/python2.7/site-packages/pydaos
%{_libdir}/python2.7/site-packages/pydaos/*.py
%if (0%{?rhel} >= 7)
%{_libdir}/python2.7/site-packages/pydaos/*.pyc
%{_libdir}/python2.7/site-packages/pydaos/*.pyo
%endif
%{_libdir}/python2.7/site-packages/pydaos/pydaos_shim_27.so
%dir  %{_libdir}/python2.7/site-packages/pydaos/raw
%{_libdir}/python2.7/site-packages/pydaos/raw/*.py
%if (0%{?rhel} >= 7)
%{_libdir}/python2.7/site-packages/pydaos/raw/*.pyc
%{_libdir}/python2.7/site-packages/pydaos/raw/*.pyo
%endif
%dir %{_libdir}/python3
%dir %{_libdir}/python3/site-packages
%dir %{_libdir}/python3/site-packages/pydaos
%{_libdir}/python3/site-packages/pydaos/*.py
%if (0%{?rhel} >= 7)
%{_libdir}/python3/site-packages/pydaos/*.pyc
%{_libdir}/python3/site-packages/pydaos/*.pyo
%endif
%{_libdir}/python3/site-packages/pydaos/pydaos_shim_3.so
%dir %{_libdir}/python3/site-packages/pydaos/raw
%{_libdir}/python3/site-packages/pydaos/raw/*.py
%if (0%{?rhel} >= 7)
%{_libdir}/python3/site-packages/pydaos/raw/*.pyc
%{_libdir}/python3/site-packages/pydaos/raw/*.pyo
%endif
%{_datadir}/%{name}/ioil-ld-opts
%config(noreplace) %{conf_dir}/daos_agent.yml
%config(noreplace) %{conf_dir}/daos.yml
%{_unitdir}/daos_agent.service
%{_mandir}/man8/daos.8*
%{_mandir}/man8/dmg.8*

%files tests
%dir %{_prefix}/lib/daos
%{_prefix}/lib/daos/TESTING
%{_bindir}/hello_drpc
%{_bindir}/*_test*
%{_bindir}/smd_ut
%{_bindir}/vea_ut
%{_bindir}/daosbench
%{_bindir}/daos_perf
%{_bindir}/daos_racer
%{_bindir}/evt_ctl
%{_bindir}/obj_ctl
%{_bindir}/daos_gen_io_conf
%{_bindir}/daos_run_io_conf
# For avocado tests
%{_prefix}/lib/daos/.build_vars.json
%{_prefix}/lib/daos/.build_vars.sh

%files devel
%{_includedir}/*
%{_libdir}/libdaos.so
%{_libdir}/*.a

%changelog
* Wed May 06 2020 Brian J. Murrell <brian.murrell@intel.com> - 0.9.4-2
- Move fuse dependencies to the client subpackage

* Mon May 04 2020 Johann Lombardi <johann.lombardi@intel.com> - 0.9.4-1
- Version bump up to 0.9.4

* Fri May 01 2020 Alexander Oganezov <alexander.a.oganezov@intel.com> - 0.9.3-1
- Version bump up to 0.9.3
- Updated to cart tag v4.7.0

* Fri Apr 17 2020 Johann Lombardi <johann.lombardi@intel.com> - 0.9.2-1
- Version bump up to 0.9.2

* Wed Apr 15 2020 Brian J. Murrell <brian.murrell@intel.com> - 0.9.1-5
- Add BR: python-distro for scons_local

* Sun Apr 11 2020 Brian J. Murrell <brian.murrell@intel.com> - 0.9.1-4
- Use distro versions of fuse and fio
- Use CaRT release 4.6.1
- Remove sha-based cart dependencies
- Remove libfabric Requires: cart will bring that in
- Remove build support for SLES12/Leap42.3

* Thu Apr 02 2020 Tom Nabarro <tom.nabarro@intel.com> 0.9.1-3
- pin version of spdk to 19.04.1 and restrict runtime version >=

* Fri Mar 27 2020 David Quigley <david.quigley@intel.com> - 0.9.1-2
- add daos and dmg man pages to the daos-client files list

* Wed Mar 18 2020 Johann Lombardi <johann.lombardi@intel.com> - 0.9.1-1
- Version bump up to 0.9.1

* Tue Mar 03 2020 Brian J. Murrell <brian.murrell@intel.com> - 0.9.0-4
- bump up go minimum version to 1.12

* Thu Feb 20 2020 Brian J. Murrell <brian.murrell@intel.com> - 0.9.0-3
- daos-server requires daos-client (same version)

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
