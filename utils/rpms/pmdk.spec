%if %{with server}
%if %{with build_deps}
%package pmdk
Summary: Low-level persistent memory support library
Group: System Environment/Libraries

%description pmdk
Package providing libraries needed by DAOS

%files pmdk
%{_libdir}/libdaospmem.so.*
%{_libdir}/libdaospmemobj.so.*
%{_libdir}/libdaospmem2.so.*
%{_libdir}/libdaospmempool.so.*
%{_bindir}/daospmem*
%{_bindir}/daosdaxio
%{_sysconfdir}/bash_completion.d/daospmempool
%{_bindir}/daospmre*
%{_datadir}/pmreorder
%license deps/pmdk/LICENSE
%doc deps/pmdk/ChangeLog deps/pmdk/CONTRIBUTING.md deps/pmdk/README.md
%exclude %{_libdir}/pmdk_debug/*
%exclude %{_libdir}/daos_internal/libpmem2*
%exclude %{_libdir}/daos_internal/libpmempool*

%package pmdk-devel
Summary: Low-level persistent memory support library
Group: System Environment/Libraries

%description pmdk-devel
Package providing libraries needed by DAOS

%files pmdk-devel
%defattr(-,root,root,-)
%{_libdir}/libdaospmem.so
%{_libdir}/libdaospmemobj.so
%{_libdir}/libdaospmem2.so
%{_libdir}/libdaospmempool.so
%{_libdir}/daos_internal/*.so*
%{_includedir}/daos_internal/libpmemobj.h
%{_includedir}/daos_internal/libpmemobj/*.h
%{_libdir}/pkgconfig/libdaospmemobj.pc
%{_libdir}/pkgconfig/libdaospmem.pc
%{_libdir}/pkgconfig/libdaospmempool.pc
%{_libdir}/pkgconfig/libdaospmem2.pc
%license deps/pmdk/LICENSE
%doc deps/pmdk/ChangeLog deps/pmdk/CONTRIBUTING.md deps/pmdk/README.md
%else
BuildRequires: daos-pmdk = %{daos_version}
%endif
%endif
