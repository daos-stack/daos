%if %{with server}
%if %{with build_deps}

%package spdk
Summary: DAOS build of spdk

%description spdk
Prebuilt DAOS SPDK dependency

%files spdk
%license deps/spdk/LICENSE
%{_libdir}/libdaosspdk*.so.*
%{_libdir}/libdaosrte*.so.*
%{_libdir}/dpdk/*/libdaosrte*.so.*
%{_bindir}/daosspdk*
%{_datadir}/daosspdk/*

%package spdk-devel
Summary: DAOS build of spdk

%description spdk-devel
Prebuilt DAOS SPDK dependency

%files spdk-devel
%{_libdir}/pkgconfig/daosspdk*.pc
%{_libdir}/pkgconfig/libdaosdpdk*.pc
%{_libdir}/libdaosspdk*.so
%{_libdir}/libdaosrte*.so
%{_libdir}/dpdk/*/libdaosrte*.so
%{_libdir}/daos_internal/libspdk*
%{_libdir}/daos_internal/librte*
%{_includedir}/daos_internal/dpdk/*
%{_includedir}/daos_internal/spdk/*

%else
BuildRequires: %{name}-spdk = %{daos_version}
%endif
%endif
