Name:          fused
Version:       1.0.0
Release:       3%{?relval}%{?dist}
Summary:       DAOS File System in Userspace Library

License:       LGPLv2+
URL:           https://github.com/daos-stack/fused
Source0:       https://github.com/daos-stack/%{name}/releases/download/%{shortcommit0}/%{name}-%{version}.tar.gz

Requires:      which
Conflicts:     filesystem < 3
BuildRequires: libselinux-devel
BuildRequires: meson, gcc-c++, gcc

%description
This package builds on FUSE but implements a completely custom file
system intended for use with the DAOS file system.

%package devel
Summary:   DAOS file system development files
Group:     System Environment/Libraries
License:   LGPLv2+
Conflicts: filesystem < 3

%global debug_package %{nil}

%description devel
Static library, pkgconfig, and headers for DAOS FUSE library

%prep
%autosetup

%build
%meson --strip -Ddisable-mtab=True -Dutils=False --default-library static
%meson_build

%install
export MESON_INSTALL_DESTDIR_PREFIX=%{buildroot}/usr %meson_install
find %{buildroot} .
find %{buildroot} -type f -name "*.la" -exec rm -f {} ';'

%files devel
%{_libdir}/libfused.a
%{_includedir}/fused/
%{_libdir}/pkgconfig

%changelog
* Sun Jan 12 2025 Jeff Olivier <jeffolivier@google.com> - 1.0.0-3.0
- Remove dependence on fused

* Sat Jan 11 2025 Jeff Olivier <jeffolivier@google.com> - 1.0.0-2.0
- Only build static lib

* Mon Feb 12 2024 Jeff Olivier <jeffolivier@google.com> - 1.0.0-1.0
- Initial packaging for fused, a DAOS file system adaptation of libfused
