Name:          cart
Version:       0.0.1
Release:       1%{?dist}
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
mkdir -p %{?buildroot}/%{_prefix}/lib/cart
cp -al multi-node-test.sh utils %{?buildroot}%{_prefix}/lib/cart/
mv %{?buildroot}%{_prefix}/{TESTING,lib/cart/}
ln %{?buildroot}%{_prefix}/lib/cart/{TESTING/.build_vars,.build_vars-Linux}.sh

%files
%defattr(-, root, root, -)
%{_bindir}/*
%{_libdir}/*
%{_prefix}/lib/cart
%{_includedir}/*
%{_prefix}%{_sysconfdir}/*
%doc



%changelog
* Wed Apr  3 2019 Brian J. Murrell <brian.murrell@intel.com>
- initial package
