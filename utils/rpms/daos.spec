%define daoshome %{_exec_prefix}/lib/%{name}
%define server_svc_name daos_server.service
%define agent_svc_name daos_agent.service

%define create_file_list() ( \
  touch %1; \
  for file in %2; \
  do \
    echo ${file#"%3"} >> %1; \
    file_name_w_ext=${file##*/}; \
    file_name=${file_name_w_ext%.*}; \
    utils=$(grep -l -R -E "(from|import) ${file_name}" %5 %3%4/ftest/util/*); \
    for util_file in ${utils}; \
    do \
      echo ${util_file#"%3"} >> %1; \
      util_name_file_w_ext=${util_file##*/}; \
      util_name=${util_name_file_w_ext%.*}; \
      regex="(from|import) (${util_name}|${file_name})"; \
      search="%5 --exclude-dir=util %3%4/ftest/*"; \
      ftests=$(grep -l -R -E "${regex}" ${search}); \
      for ftest_file in ${ftests}; \
      do \
        for other_file in $(find ${ftest_file%%.*}.* | sort); \
        do \
          echo "${other_file#%3}" >> %1; \
        done; \
      done; \
    done; \
  done; \
  cat %1 | sort | uniq > %{1}_unique; \
  mv %{1}_unique %1; \
  cat %1 \
)

%if (0%{?suse_version} >= 1500)
# until we get an updated mercury build on 15.2
%global mercury_version 2.0.0~rc1-1.suse.lp151
%else
%global mercury_version 2.0.0~rc1-1%{?dist}
%endif

Name:          daos
Version:       1.1.1
Release:       3%{?relval}%{?dist}
Summary:       DAOS Storage Engine

License:       Apache
URL:           https//github.com/daos-stack/daos
Source0:       %{name}-%{version}.tar.gz

BuildRequires: scons >= 2.4
BuildRequires: libfabric-devel >= 1.11
BuildRequires: boost-devel
BuildRequires: mercury-devel = %{mercury_version}
BuildRequires: openpa-devel
BuildRequires: libpsm2-devel
BuildRequires: gcc-c++
BuildRequires: openmpi3-devel
BuildRequires: hwloc-devel
%if (0%{?rhel} >= 7)
BuildRequires: argobots-devel >= 1.0rc1
BuildRequires: json-c-devel
%else
BuildRequires: libabt-devel >= 1.0rc1
BuildRequires: libjson-c-devel
%endif
BuildRequires: libpmem-devel >= 1.8, libpmemobj-devel >= 1.8
BuildRequires: fuse3-devel >= 3.4.2
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
BuildRequires: spdk-devel >= 20, spdk-devel < 21
%if (0%{?rhel} >= 7)
BuildRequires: libisa-l-devel
BuildRequires: libisa-l_crypto-devel
%else
BuildRequires: libisal-devel
BuildRequires: libisal_crypto-devel
%endif
BuildRequires: raft-devel = 0.6.0
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
BuildRequires: python-devel python3-devel
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
%endif # 0%{?is_opensuse}
%endif # (0%{?suse_version} >= 1315)
%endif # (0%{?rhel} >= 7)
%if (0%{?suse_version} >= 1500)
Requires: libpmem1 >= 1.8, libpmemobj1 >= 1.8
%else
Requires: libpmem >= 1.8, libpmemobj >= 1.8
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
Requires: %{name} = %{version}-%{release}
Requires: %{name}-client = %{version}-%{release}
Requires: spdk-tools
Requires: ndctl
Requires: ipmctl
Requires: hwloc
Requires: mercury = %{mercury_version}
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
Requires: libfabric >= 1.8.0
%systemd_requires

%description server
This is the package needed to run a DAOS server

%package client
Summary: The DAOS client
Requires: %{name} = %{version}-%{release}
Requires: mercury = %{mercury_version}
Requires: libfabric >= 1.8.0
Requires: fuse3 >= 3.4.2
%if (0%{?suse_version} >= 1500)
Requires: libfuse3-3 >= 3.4.2
%else
# because our repo has a deprecated fuse-3.x RPM, make sure we don't
# get it when fuse3 Requires: /etc/fuse.conf
Requires: fuse < 3, fuse3-libs >= 3.4.2
%endif
%systemd_requires

%description client
This is the package needed to run a DAOS client

%package tests
Summary: The DAOS test suite
Requires: %{name}-client = %{version}-%{release}
Requires: python-pathlib
Requires: python2-tabulate
Requires: mpich
Requires: openmpi3
Requires: ndctl
Requires: hwloc
%if (0%{?suse_version} >= 1315)
Requires: libpsm_infinipath1
%endif

%description tests
This is the package needed to run the DAOS test suite

%package tests-ior
Summary: The DAOS test suite for ior tests
Requires: %{name}-tests = %{version}-%{release}
Requires: ior-hpc-daos-0
Requires: hdf5-mpich2-tests-daos-0
Requires: hdf5-openmpi3-tests-daos-0
Requires: hdf5-vol-daos-mpich2-tests-daos-0
Requires: hdf5-vol-daos-openmpi3-tests-daos-0

%description tests-ior
This is the package needed to run the DAOS test suite with ior

%package tests-fio
Summary: The DAOS test suite for fio tests
Requires: %{name}-tests = %{version}-%{release}
Requires: fio

%description tests-fio
This is the package needed to run the DAOS test suite with fio

%package tests-mpiio
Summary: The DAOS test suite for mpiio tests
Requires: %{name}-tests-ior = %{version}-%{release}
Requires: romio-tests-cart-4-daos-0
Requires: testmpio-cart-4-daos-0
Requires: mpi4py-tests-cart-4-daos-0

%description tests-mpiio
This is the package needed to run the DAOS test suite with mpiio

%package tests-macsio
Summary: The DAOS test suite for macsio tests
Requires: %{name}-tests-mpiio = %{version}-%{release}
Requires: MACSio-mpich2-daos-0
Requires: MACSio-openmpi3-daos-0

%description tests-macsio
This is the package needed to run the DAOS test suite with  macsio

%package tests-soak
Summary: The DAOS soak test suite
Requires: %{name}-tests = %{version}-%{release}
Requires: %{name}-tests-ior = %{version}-%{release}
Requires: %{name}-tests-fio = %{version}-%{release}
Requires: slurm

%description tests-soak
This is the package needed to run the DAOS soak test suite

%package devel
# Leap 15 doesn't seem to be creating dependencies as richly as EL7
# for example, EL7 automatically adds:
# Requires: libdaos.so.0()(64bit)
%if (0%{?suse_version} >= 1500)
Requires: %{name}-client = %{version}-%{release}
Requires: %{name} = %{version}-%{release}
%endif
Requires: libuuid-devel
Requires: libyaml-devel
Requires: boost-devel
# Pin mercury to exact version during development
#Requires: mercury-devel < 2.0.0a1
# we ideally want to set this minimum version however it seems to confuse yum:
# https://github.com/rpm-software-management/yum/issues/124
#Requires: mercury >= 2.0.0~a1
Requires: mercury-devel = %{mercury_version}
Requires: openpa-devel
Requires: hwloc-devel
Summary: The DAOS development libraries and headers

%description devel
This is the package needed to build software with the DAOS library.

%prep
%setup -q

%build

%define conf_dir %{_sysconfdir}/daos

scons %{?_smp_mflags}      \
      --config=force       \
      --no-rpath           \
      USE_INSTALLED=all    \
      CONF_DIR=%{conf_dir} \
      PREFIX=%{?buildroot}

%install
scons %{?_smp_mflags}                 \
      --config=force                  \
      --no-rpath                      \
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
install -m 644 utils/systemd/%{server_svc_name} %{?buildroot}/%{_unitdir}
install -m 644 utils/systemd/%{agent_svc_name} %{?buildroot}/%{_unitdir}
mkdir -p %{?buildroot}/%{conf_dir}/certs/clients
mv %{?buildroot}/%{_prefix}/etc/bash_completion.d %{?buildroot}/%{_sysconfdir}

output="daos-tests-ior.files"
ftest_path=%{?_prefix}/lib/daos/TESTING
files=(%{?buildroot}/%{?_prefix}/lib/daos/TESTING/ftest/util/ior_utils.py \
  %{?buildroot}/%{?_prefix}/lib/daos/TESTING/ftest/util/nvme_utils.py)
exclude="--exclude-dir=soak"
%create_file_list  ${output} ${files} %{?buildroot} ${ftest_path} ${exclude}

output="daos-tests-fio.files"
exclude="${exclude} --exclude=ior_utils.py --exclude=ior_test_base.py"
files=%{?buildroot}/%{?_prefix}/lib/daos/TESTING/ftest/util/fio_utils.py
%create_file_list ${output} ${files} %{?buildroot} ${ftest_path} ${exclude}

output="daos-tests-mpiio.files"
exclude="${exclude} --exclude=fio_utils.py"
files=%{?buildroot}/%{?_prefix}/lib/daos/TESTING/ftest/util/mpio_utils.py
%create_file_list ${output} ${files} %{?buildroot} ${ftest_path} ${exclude}

output="daos-tests-macsio.files"
exclude="${exclude} --exclude=mpiio_utils.py"
files=%{?buildroot}/%{?_prefix}/lib/daos/TESTING/ftest/util/macsio_utils.py
%create_file_list ${output} ${files} %{?buildroot} ${ftest_path} ${exclude}

touch daos-tests-soak.files
for file in $(find %{?buildroot}${ftest_path}/ftest/soak -type f | sort)
do
  echo ${file#%{?buildroot}} >> daos-tests-soak.files
done
cat daos-tests-soak.files

touch daos-tests.files
for file in $(find %{?buildroot}${ftest_path}/ftest -type f | sort)
do
  echo ${file#%{?buildroot}} >> daos-tests.files
done
for name in ior fio mpiio macsio soak
do
  grep -Fvxf daos-tests-${name}.files daos-tests.files > daos-tests.files_new
  mv daos-tests.files{_new,}
done
cat daos-tests.files

%if (0%{?rhel} >= 7)
for name in tests tests-ior tests-fio tests-mpiio tests-macsio tests-soak
do
  file="daos-${name}.files"
  cat ${file} | sed -ne 's/\(.*\)\.py/\1.pyc\n\1.pyo/p' >> ${file}
  sort ${file}
done
%endif

%pre server
getent group daos_admins >/dev/null || groupadd -r daos_admins
getent passwd daos_server >/dev/null || useradd -s /sbin/nologin -r daos_server
%post server
/sbin/ldconfig
%systemd_post %{server_svc_name}
%preun server
%systemd_preun %{server_svc_name}
%postun server
/sbin/ldconfig
%systemd_postun %{server_svc_name}

%pre client
getent passwd daos_agent >/dev/null || useradd -s /sbin/nologin -r daos_agent
%post client
%systemd_post %{agent_svc_name}
%preun client
%systemd_preun %{agent_svc_name}
%postun client
%systemd_postun %{agent_svc_name}

%files
%defattr(-, root, root, -)
# you might think libvio.so goes in the server RPM but
# the 2 tools following it need it
%{_libdir}/daos_srv/libbio.so
# you might think libdaos_tests.so goes in the tests RPM but
# the 4 tools following it need it
%{_libdir}/libdaos_tests.so
%{_bindir}/io_conf
%{_bindir}/jump_pl_map
%{_bindir}/ring_pl_map
%{_bindir}/pl_bench
%{_bindir}/rdbt
%{_libdir}/libvos.so
%{_libdir}/libcart*
%{_libdir}/libgurt*
%{_prefix}/etc/memcheck-cart.supp
%dir %{_prefix}%{_sysconfdir}
%{_prefix}%{_sysconfdir}/vos_size_input.yaml
%dir %{_sysconfdir}/bash_completion.d
%{_sysconfdir}/bash_completion.d/daos.bash
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
%dir %{conf_dir}/certs
%attr(0700,daos_server,daos_server) %{conf_dir}/certs
%dir %{conf_dir}/certs/clients
%attr(0700,daos_server,daos_server) %{conf_dir}/certs/clients
%attr(0644,root,root) %{conf_dir}/daos_server.yml
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
%{_unitdir}/%{server_svc_name}

%files client
%{_prefix}/etc/memcheck-daos-client.supp
%{_bindir}/cart_ctl
%{_bindir}/self_test
%{_bindir}/dmg
%{_bindir}/daos_agent
%{_bindir}/dfuse
%{_bindir}/daos
%{_bindir}/dfuse_hl
%{_bindir}/daos_storage_estimator.py
%{_libdir}/*.so.*
%{_libdir}/libdfs.so
%{_libdir}/%{name}/API_VERSION
%{_libdir}/libduns.so
%{_libdir}/libdfuse.so
%{_libdir}/libioil.so
%{_libdir}/libdfs_internal.so
%{_libdir}/libvos_size.so
%dir  %{_libdir}/python2.7/site-packages/pydaos
%dir  %{_libdir}/python2.7/site-packages/storage_estimator
%{_libdir}/python2.7/site-packages/pydaos/*.py
%{_libdir}/python2.7/site-packages/storage_estimator/*.py
%if (0%{?rhel} >= 7)
%{_libdir}/python2.7/site-packages/pydaos/*.pyc
%{_libdir}/python2.7/site-packages/pydaos/*.pyo
%{_libdir}/python2.7/site-packages/storage_estimator/*.pyc
%{_libdir}/python2.7/site-packages/storage_estimator/*.pyo
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
%dir %{_libdir}/python3/site-packages/storage_estimator
%{_libdir}/python3/site-packages/pydaos/*.py
%{_libdir}/python3/site-packages/storage_estimator/*.py
%if (0%{?rhel} >= 7)
%{_libdir}/python3/site-packages/pydaos/*.pyc
%{_libdir}/python3/site-packages/pydaos/*.pyo
%{_libdir}/python3/site-packages/storage_estimator/*.pyc
%{_libdir}/python3/site-packages/storage_estimator/*.pyo
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
%config(noreplace) %{conf_dir}/daos_control.yml
%{_unitdir}/%{agent_svc_name}
%{_mandir}/man8/daos.8*
%{_mandir}/man8/dmg.8*

%files tests -f daos-tests.files
%dir %{_prefix}/lib/daos
%{_prefix}/lib/daos/TESTING/scripts
%{_prefix}/lib/daos/TESTING/tests
%{_bindir}/hello_drpc
%{_bindir}/*_test*
%{_bindir}/smd_ut
%{_bindir}/vea_ut
%{_bindir}/daos_perf
%{_bindir}/daos_racer
%{_bindir}/evt_ctl
%{_bindir}/obj_ctl
%{_bindir}/daos_gen_io_conf
%{_bindir}/daos_run_io_conf
%{_bindir}/crt_launch
%{_prefix}/etc/fault-inject-cart.yaml
# For avocado tests
%{_prefix}/lib/daos/.build_vars.json
%{_prefix}/lib/daos/.build_vars.sh

%files tests-ior -f daos-tests-ior.files

%files tests-fio -f daos-tests-fio.files

%files tests-mpiio -f daos-tests-mpiio.files

%files tests-macsio -f daos-tests-macsio.files

%files tests-soak -f daos-tests-soak.files

%files devel
%{_includedir}/*
%{_libdir}/libdaos.so
%{_libdir}/*.a

%changelog
* Tue Oct 13 2020 Phillip Henderson <phillip.henderson@intel.com> 1.1.1-3
- Separated the daos-tests package into multiple packages based upon external
  package requirements.

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
