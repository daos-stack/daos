%if %{with server}
%if %{with build_deps}

%package spdk
Summary: DAOS build of spdk

%description spdk
Prebuilt DAOS SPDK dependency

%files spdk
%license deps/spdk/LICENSE
%{_libdir}/daos_srv/libspdk*.so.*
%{_libdir}/daos_srv/librte*.so.*
%{_libdir}/daos_srv/dpdk/*/librte*.so.*
%{_bindir}/spdk*
%{_datadir}/spdk/*

%package spdk-devel
Summary: DAOS build of spdk

%description spdk-devel
Prebuilt DAOS SPDK dependency

%files spdk-devel
%{_libdir}/daos_srv/pkgconfig/spdk*.pc
%{_libdir}/daos_srv/pkgconfig/libdpdk*.pc
%{_libdir}/daos_srv/libspdk*.so
%{_libdir}/daos_srv/librte*.so
%{_libdir}/daos_srv/dpdk/*/librte*.so
%{_includedir}/daos_internal/dpdk/*
%{_includedir}/daos_internal/spdk/*

%else
BuildRequires: %{name}-spdk = %{daos_version}
%endif
%endif
