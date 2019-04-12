%define carthome %{_exec_prefix}/lib/%{name}

Name:          cart
Version:       0.0.1
Release:       2%{?dist}
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
Requires: boost
Requires: libuuid
Requires: hwloc
Requires: openssl
Requires: libyaml

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

scons %{?_smp_mflags}                     \
      --config=force                      \
      USE_INSTALLED=all                   \
      PREFIX=%{?buildroot}

%install
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
%{_libdir}/*
%{carthome}/utils
%{_prefix}%{_sysconfdir}/*
%doc

%files devel
%{_includedir}/*

%files tests
%{carthome}/TESTING
%{carthome}/multi-node-test.sh
%{carthome}/.build_vars-Linux.sh

%changelog
* Fri Apr 05 2019 Brian J. Murrell <brian.murrell@intel.com>
- split out devel and tests subpackages
- have devel depend on the main package since we only have the
  unversioned library at the moement which is in the main package

* Wed Apr 03 2019 Brian J. Murrell <brian.murrell@intel.com>
- initial package
