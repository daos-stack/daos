%package -n mercury
Summary:  Mercury package
Version: %{mercury_version}
Release:  %{mercury_release}%{?relval}%{?dist}

%description -n mercury
Mercury is a Remote Procedure Call (RPC) framework specifically
designed for use in High-Performance Computing (HPC) systems with
high-performance fabrics. Its network implementation is abstracted
to make efficient use of native transports and allow easy porting
to a variety of systems. Mercury supports asynchronous transfer of
parameters and execution requests, and has dedicated support for
large data arguments that are transferred using Remote Memory
Access (RMA). Its interface is generic and allows any function
call to be serialized. Since code generation is done using the C
preprocessor, no external tool is required.

%package -n mercury-devel
Summary:  Mercury devel package
Version: %{mercury_version}
Release:  %{mercury_release}%{?relval}%{?dist}
Requires: mercury%{?_isa} = %{mercury_version}

%description -n mercury-devel
Mercury development headers and libraries.

%if %{with ucx}
%package -n mercury-ucx
Summary:  Mercury with UCX
Version: %{mercury_version}
Release:  %{mercury_release}%{?relval}%{?dist}
Requires: mercury%{?_isa} = %{mercury_version}

%description -n mercury-ucx
Mercury plugin to support the UCX transport.
%endif

%files -n mercury
%license deps/mercury/LICENSE.txt
%doc deps/mercury/Documentation/CHANGES.md
%{_bindir}/hg_*
%{_bindir}/na_*
%{_libdir}/libmercury*.so.*
%{_libdir}/libna*.so.*
%{_libdir}/mercury/libna_plugin_ofi.so

%if %{with ucx}
%files -n mercury-ucx
%{_libdir}/mercury/libna_plugin_ucx.so
%endif

%files -n mercury-devel
%license deps/mercury/LICENSE.txt
%doc deps/mercury/README.md
%{_includedir}/mercury*
%{_includedir}/na_*
%{_libdir}/libmercury.so
%{_libdir}/libmercury_util.so
%{_libdir}/libna.so
%{_libdir}/pkgconfig/mercury*.pc
%{_libdir}/pkgconfig/na*.pc
%{_libdir}/cmake/*
