%if (0%{?suse_version} > 0)
%define libfabric_name libfabric1
%else
%define libfabric_name libfabric
%endif

%if %{with build_deps}
%package -n     %{libfabric_name}
Summary:        Shared library for libfabric
Version:        %{libfabric_version}
Release:        %{libfabric_release}%{?relval}%{?dist}
Group:          System/Libraries

%description -n %{libfabric_name}
%{name}-%{libfabric_name} provides a user-space API to access high-performance fabric
services, such as RDMA. This package contains the runtime library.

%package  -n    %{libfabric_name}-devel
Summary:        Development files for %{name}
Version:        %{libfabric_version}
Release:        %{libfabric_release}%{?relval}%{?dist}
Group:          Development/Libraries/C and C++
Requires:       %{libfabric_name}%{?_isa} = %{libfabric_version}

%description -n   %{libfabric_name}-devel
The %{libfabric_name}-devel package contains libraries and header files for
developing applications that use %{libfabric_name}.

%files -n %{libfabric_name}
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
%files -n %{libfabric_name}
%defattr(-,root,root)
%{_libdir}/libfabric*.so.1*
%endif

%files -n %{libfabric_name}-devel
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
%else
BuildRequires: %{libfabric_name}-devel >= %{libfabric_version}
%endif

