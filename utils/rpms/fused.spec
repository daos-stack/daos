%if %{with build_deps}
%package fused-devel
Summary: Static library and headers for DAOS fuse

%description fused-devel
Static library, pkgconfig, and headers for DAOS FUSE library

%files fused-devel
%{_libdir}/libfused.a
%{_includedir}/fused/
%{_libdir}/pkgconfig/fused.pc
%license deps/fused/LICENSE

%else
BuildRequires: fused-devel = %{daos_version}
%endif
