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
%license deps/pmdk/LICENSE
%doc deps/pmdk/ChangeLog deps/pmdk/CONTRIBUTING.md deps/pmdk/README.md
%exclude %{_libdir}/libpmem*.a
%exclude %{_datadir}/pmemreorder
%exclude %{_includedir}/libpmem2*
%exclude %{_includedir}/libpmempool.h
%exclude %{_libdir}/libpmem2*
%exclude %{_libdir}/libpmempool*
%exclude %{_bindir}/pmem*

%package pmdk-devel
Summary: Low-level persistent memory support library
Group: System Environment/Libraries

%description pmdk-devel
Package providing libraries needed by DAOS

%files pmdk-devel
%defattr(-,root,root,-)
%{_libdir}/libdaospmem.so
%{_libdir}/libdaospmemobj.so
%{_libdir}/pkgconfig/libdaospmemobj.pc
%{_libdir}/pkgconfig/libdaospmem.pc
%{_includedir}/daos_srv/libpmemobj.h
%{_includedir}/daos_srv/libpmemobj/*.h
%license deps/pmdk/LICENSE
%doc deps/pmdk/ChangeLog deps/pmdk/CONTRIBUTING.md deps/pmdk/README.md
%else
BuildRequires: daos-pmdk = %{daos_version}
%endif
%endif
