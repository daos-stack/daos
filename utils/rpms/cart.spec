%define carthome %{_exec_prefix}/lib/%{name}

Name:          cart
Version:       4.3.1
Release:       1%{?relval}%{?dist}
Summary:       CaRT

License:       Apache
URL:           https://github.com/daos-stack/cart
Source0:       %{name}-%{version}.tar.gz
Source1:       scons_local-%{version}.tar.gz

BuildRequires: scons >= 2.4
BuildRequires: libfabric-devel
BuildRequires: openpa-devel
BuildRequires: mercury-devel = 1.0.1-21%{?dist}
BuildRequires: openmpi3-devel
BuildRequires: libpsm2-devel
BuildRequires: libevent-devel
BuildRequires: boost-devel
BuildRequires: libuuid-devel
BuildRequires: hwloc-devel
BuildRequires: openssl-devel
BuildRequires: libcmocka-devel
BuildRequires: libyaml-devel
%if (0%{?suse_version} >= 1315)
# these are needed to prefer packages that both provide the same requirement
# prefer over libpsm2-compat
BuildRequires: libpsm_infinipath1
# prefer over libcurl4-mini
BuildRequires: libcurl4
BuildRequires: Modules
%endif
BuildRequires: gcc-c++
%if %{defined sha1}
Provides: %{name}-%{sha1}
%endif

%description
Collective and RPC Transport (CaRT)

CaRT is an open-source RPC transport layer for Big Data and Exascale
HPC. It supports both traditional P2P RPC delivering and collective RPC
which invokes the RPC at a group of target servers with a scalable
tree-based message propagating.

%package devel
Summary: CaRT devel

# since the so is unversioned, it only exists in the main package
# at this time
Requires: %{name} = %{version}-%{release}
Requires: libuuid-devel
Requires: libyaml-devel
Requires: boost-devel
Requires: mercury-devel = 1.0.1-21%{?dist}
Requires: openpa-devel
Requires: libfabric-devel
Requires: openmpi3-devel
Requires: hwloc-devel
%if %{defined sha1}
Provides: %{name}-devel-%{sha1}
%endif

%description devel
CaRT devel

%package tests
Summary: CaRT tests

Requires: %{name} = %{version}-%{release}
%if %{defined sha1}
Provides: %{name}-tests-%{sha1}
%endif

%description tests
CaRT tests

%prep
%setup -q -a 1


%build
# remove rpathing from the build
find . -name SConscript | xargs sed -i -e '/AppendUnique(RPATH=.*)/d'

SL_PREFIX=%{_prefix}                      \
scons %{?_smp_mflags}                     \
      --config=force                      \
      USE_INSTALLED=all                   \
      PREFIX=%{?buildroot}%{_prefix}

%install
SL_PREFIX=%{_prefix}                      \
scons %{?_smp_mflags}                     \
      --config=force                      \
      install                             \
      USE_INSTALLED=all                   \
      PREFIX=%{?buildroot}%{_prefix}
BUILDROOT="%{?buildroot}"
PREFIX="%{?_prefix}"
sed -i -e s/${BUILDROOT//\//\\/}[^\"]\*/${PREFIX//\//\\/}/g %{?buildroot}%{_prefix}/TESTING/.build_vars.*
mv %{?buildroot}%{_prefix}/lib{,64}
#mv %{?buildroot}/{usr/,}etc
mkdir -p %{?buildroot}/%{carthome}
cp -al multi-node-test.sh utils %{?buildroot}%{carthome}/
mv %{?buildroot}%{_prefix}/{TESTING,lib/cart/}
ln %{?buildroot}%{carthome}/{TESTING/.build_vars,.build_vars-Linux}.sh

#%if 0%{?suse_version} >= 01315
#%post -n %{suse_libname} -p /sbin/ldconfig
#%postun -n %{suse_libname} -p /sbin/ldconfig
#%else
%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig
#%endif

%files
%defattr(-, root, root, -)
%{_bindir}/*
%{_libdir}/*.so.*
%dir %{carthome}
%{carthome}/utils
%dir %{_prefix}%{_sysconfdir}
%{_prefix}%{_sysconfdir}/*
%doc

%files devel
%{_includedir}/*
%{_libdir}/libcart.so
%{_libdir}/libgurt.so

%files tests
%{carthome}/TESTING
%{carthome}/multi-node-test.sh
%{carthome}/.build_vars-Linux.sh

%changelog
* Thu Dec 26 2019 Alexander Oganezov <alexander.a.oganezov@intel.com> - 4.3.1-1
- Libcart version 4.3.1-1
- ofi+verbs provider no longer supported; 'ofi+verbs;ofi_rxm' to be used instead

* Mon Dec 16 2019 Alexander Oganezov <alexander.a.oganezov@intel.com> - 4.3.0-1
- Libcart version 4.3.0-1

* Sat Dec 14 2019 Jeff Olivier <jeffrey.v.olivier@intel.com> - 4.2.0-1
- Libcart version 4.2.0-1
- More modifications to cart build that may affect downstream components

* Wed Dec 11 2019 Jeff Olivier <jeffrey.v.olivier@intel.com> - 4.1.0-1
- Libcart version 4.1.0-1
- OpenMPI build modified to use installed packages
- Add BR: libpsm2-devel since we don't get that with ompi-devel now

* Mon Dec 9 2019 Alexander Oganezov <alexander.a.oganezov@intel.com> - 4.0.0-1
- Libcart version 4.0.0-1
- PMIX support removed along with all associated code

* Thu Dec 5 2019 Alexander Oganezov <alexander.a.oganezov@intel.com> - 3.2.0-3
- Libcart version 3.2.0-3
- Restrict mercury to be version = 1.0.1-21

* Tue Dec 3 2019 Alexander Oganezov <alexander.a.oganezov@intel.com> - 3.2.0-2
- Libcart version 3.2.0-2
- Restrict mercury used to be < 1.0.1-21

* Thu Nov 21 2019 Alexander Oganezov <alexander.a.oganezov@intel.com> - 3.2.0-1
- Libcart version 3.2.0-1
- New DER_GRPVER error code added

* Tue Nov 19 2019 Alexander Oganezov <alexander.a.oganezov@intel.com> - 3.1.0-1
- Libcart version 3.1.0-1
- New crt_group_version_set() API added

* Wed Nov 13 2019 Alexander Oganezov <alexander.a.oganezov@intel.com> - 3.0.0-1
- Libcart version 3.0.0-1
- IV namespace APIs changed

* Mon Nov 11 2019 Brian J. Murrell <brian.murrell@intel.com> - 2.1.0-2
- Don't R: ompi-devel from cart-devel as it breaks the ior
  build which ends up building with ompi instead of mpich
  - the correct solution here is to environment-module-ize
    ompi

* Mon Nov 11 2019 Jeff Olivier <jeffrey.v.olivier@intel.com> - 2.1.0-1
- Libcart version 2.1.0-1
- Add support for registering error codes

* Wed Oct 30 2019 Alexander Oganezov <alexander.a.oganezov@intel.com> - 2.0.0-1
- Libcart version 2.0.0-1
- crt_group_primary_modify, crt_group_secondary_modify APIs changed

* Thu Oct 24 2019 Brian J. Murrell <brian.murrell@intel.com> - 1.6.0-2
- Add BRs to prefer packages that have choices
- Add BR for scons >= 2.4 and gcc-c++
- Add some dirs to %files so they are owned by a package
- Don't unpack the cart tarball twice

* Wed Oct 23 2019 Alexander Oganezov <alexander.a.oganezov@intel.com>
- Libcart version 1.6.0

* Thu Oct 17 2019 Alexander Oganezov <alexander.a.oganezov@intel.com>
- Libcart version 1.5.0

* Wed Oct 16 2019 Alexander Oganezov <alexander.a.oganezov@intel.com>
- Libcart version 1.4.0-2

* Mon Oct 07 2019 Ryon Jensen  <ryon.jensen@intel.com>
- Libcart version 1.4.0

* Wed Sep 25 2019 Dmitry Eremin <dmitry.eremin@intel.com>
- Libcart version 1.3.0

* Mon Sep 23 2019 Jeffrey V. Olivier <jeffrey.v.olivier@intel.com>
- Libcart version 1.2.0

* Thu Aug 08 2019 Alexander A. Oganezov <alexander.a.oganezov@intel.com>
- Libcart version 1.1.0

* Wed Aug 07 2019 Brian J. Murrell <brian.murrell@intel.com>
- Add git hash and commit count to release

* Fri Jul 26 2019 Alexander A. Oganezov <alexander.a.oganezov@intel.com>
- Libcart version 1.0.0

* Fri Jun 21 2019 Brian J. Murrell <brian.murrell@intel.com>
- add Requires: libyaml-devel to the -devel package

* Wed Jun 12 2019 Vikram Chhabra  <vikram.chhabra@intel.com>
- added versioning for libcart and libgurt

* Tue May 07 2019 Brian J. Murrell <brian.murrell@intel.com>
- update for SLES 12.3:
  - libuuid -> libuuid1

* Thu May 02 2019 Brian J. Murrell <brian.murrell@intel.com>
- fix build to use _prefix as install does

* Fri Apr 05 2019 Brian J. Murrell <brian.murrell@intel.com>
- split out devel and tests subpackages
- have devel depend on the main package since we only have the
  unversioned library at the moement which is in the main package

* Wed Apr 03 2019 Brian J. Murrell <brian.murrell@intel.com>
- initial package
