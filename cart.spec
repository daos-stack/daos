%define carthome %{_exec_prefix}/lib/%{name}

Name:          cart
Version:       1.1.0
Release:       1%{?relval}%{?dist}
Summary:       CaRT

License:       Apache
URL:           https//github.com/daos-stack/cart
Source0:       %{name}-%{version}.tar.gz
Source1:       scons_local-%{version}.tar.gz

BuildRequires: scons
BuildRequires: libfabric-devel
BuildRequires: pmix-devel
BuildRequires: openpa-devel
BuildRequires: mercury-devel
BuildRequires: ompi-devel
BuildRequires: libevent-devel
BuildRequires: boost-devel
BuildRequires: libuuid-devel
BuildRequires: hwloc-devel
BuildRequires: openssl-devel
BuildRequires: libcmocka-devel
BuildRequires: libyaml-devel
Requires: libfabric
Requires: pmix
Requires: openpa
Requires: mercury
Requires: ompi
Requires: libevent
%if (0%{?rhel} >= 7)
Requires:libuuid
Requires: libyaml
%else
%if (0%{?suse_version} >= 1315)
Requires: libuuid1
Requires: libyaml-0-2
%endif
%endif
Requires: hwloc
Requires: openssl

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

%description devel
CaRT devel

%package tests
Summary: CaRT tests

Requires: %{name} = %{version}-%{release}

%description tests
CaRT tests

%prep
%setup -q
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

%files
%defattr(-, root, root, -)
%{_bindir}/*
%{_libdir}/*.so.*
%{carthome}/utils
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
