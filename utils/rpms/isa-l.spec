%if 0%{?suse_version} >= 1315
%define isal_libname libisal2
%define isal_devname libisal-devel
%else
%define isal_libname libisa-l
%define isal_devname libisa-l-devel
%endif

%package -n isa-l
Summary:	Intelligent Storage Acceleration Library
Version: %{isal_version}
Release:  %{isal_release}%{?relval}%{?dist}
License:	BSD-3-Clause

%description -n isa-l
Provides various algorithms for erasure coding, crc, raid, compression and
 decompression

%package -n %{isal_libname}
Summary: Dynamic library for isa-l functions
Version: %{isal_version}
Release:  %{isal_release}%{?relval}%{?dist}
License: BSD-3-Clause
Requires: isa-l%{?_isa} = %{isal_version}

%description -n %{isal_libname}
This package contains the libisal.so dynamic library which contains
a collection of optimized low-level functions targeting storage
applications. ISA-L includes:
- Erasure codes - Fast block Reed-Solomon type erasure codes for any
encode/decode matrix in GF(2^8).
- CRC - Fast implementations of cyclic redundancy check. Six different
polynomials supported.
    - iscsi32, ieee32, t10dif, ecma64, iso64, jones64.
- Raid - calculate and operate on XOR and P+Q parity found in common
RAID implementations.
- Compression - Fast deflate-compatible data compression.
- De-compression - Fast inflate-compatible data compression.

%package -n %{isal_devname}
Summary:	ISA-L devel package
Version:        %{isal_version}
Release:        %{isal_release}%{?relval}%{?dist}
Requires:	%{isal_libname}%{?_isa} = %{isal_version}
Provides:	%{isal_libname}-static%{?_isa} = %{isal_version}
Obsoletes:      %{isal_devname} < %{isal_version}

%description -n %{isal_devname}
Development files for the %{isal_libname} library.

%files -n isa-l
%license LICENSE
%{_bindir}/igzip
%{_mandir}/man1/igzip.*

%files -n %{isal_libname}
%license deps/isal/LICENSE
%{_libdir}/libisal.so.*

%files -n %{isal_devname}
%license deps/isal/LICENSE
%dir %{_includedir}/isa-l
%{_includedir}/isa-l.h
%{_libdir}/libisal.so
%{_libdir}/pkgconfig/libisal.pc

