%if %{with server}
%if %{with build_deps}

%package spdk
Summary: DAOS build of spdk

%description spdk
Prebuilt DAOS SPDK dependency

%files spdk
%license deps/spdk/LICENSE
%{_libdir}/libspdk*.so.*
%{_libdir}/librte*.so.*
%{_libdir}/dpdk/*/librte*.so.*
%{_bindir}/spdk*
%{_datadir}/spdk/*

%package spdk-devel
Summary: DAOS build of spdk

%description spdk-devel
Prebuilt DAOS SPDK dependency

%files spdk-devel
%{_libdir}/pkgconfig/spdk*.pc
%{_libdir}/pkgconfig/libdpdk*.pc
%{_libdir}/libspdk*.so
%{_libdir}/librte*.so
%{_libdir}/dpdk/*/librte*.so
%{_includedir}/daos_internal/dpdk/*
%{_includedir}/daos_internal/spdk/*

%else
BuildRequires: %{name}-spdk = %{daos_version}
%endif
%endif
