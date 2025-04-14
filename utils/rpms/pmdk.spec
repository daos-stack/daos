%if %{with server}
%if %{with build_deps}
%package pmdk
Summary: Low-level persistent memory support library
Group: System Environment/Libraries

%description pmdk
Package providing libraries needed by DAOS

%files pmdk
%defattr(-,root,root,-)
%{_libdir}/daos_srv/libpmem.so.*
%{_libdir}/daos_srv/libpmemobj.so.*
%{_libdir}/daos_srv/libpmem2.so.*
%{_libdir}/daos_srv/libpmempool.so.*
%{_bindir}/pmem*
%{_bindir}/daxio
%{_sysconfdir}/bash_completion.d/pmempool
%{_bindir}/pmre*
%{_datadir}/pmreorder
%license deps/pmdk/LICENSE
%doc deps/pmdk/ChangeLog deps/pmdk/CONTRIBUTING.md deps/pmdk/README.md
%exclude %{_libdir}/daos_srv/pmdk_debug/*

%package pmdk-devel
Summary: Low-level persistent memory support library
Group: System Environment/Libraries

%description pmdk-devel
Package providing libraries needed by DAOS

%files pmdk-devel
%defattr(-,root,root,-)
%{_libdir}/libpmem.so
%{_libdir}/libpmemobj.so
%{_libdir}/libpmem2.so
%{_libdir}/libpmempool.so
%{_includedir}/daos_internal/libpmemobj.h
%{_includedir}/daos_internal/libpmemobj/*.h
%{_libdir}/pkgconfig/libpmemobj.pc
%{_libdir}/pkgconfig/libpmem.pc
%{_libdir}/pkgconfig/libpmempool.pc
%{_libdir}/pkgconfig/libpmem2.pc
%license deps/pmdk/LICENSE
%doc deps/pmdk/ChangeLog deps/pmdk/CONTRIBUTING.md deps/pmdk/README.md
%else
BuildRequires: daos-pmdk = %{daos_version}
%endif
%endif
