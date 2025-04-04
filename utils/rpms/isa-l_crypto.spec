%if 0%{?suse_version} >= 1315
%define isal_crypto_libname libisal_crypto2
%define isal_crypto_devname libisal_crypto-devel
%else
%define isal_crypto_libname libisa-l_crypto
%define isal_crypto_devname libisa-l_crypto-devel
%endif

%package -n %{isal_crypto_libname}
Summary: Dynamic library for isa-l_crypto functions
Version:        %{isal_crypto_version}
Release:        %{isal_crypto_release}%{?relval}%{?dist}
License: BSD-3-Clause
Obsoletes: %{isal_crypto_libname} < %{isal_crypto_version}

%description -n %{isal_crypto_libname}
ISA-L_crypto is a collection of optimized low-level functions
targeting storage applications. ISA-L_crypto includes:
- Multi-buffer hashes - run multiple hash jobs together on one core
for much better throughput than single-buffer versions. (
SHA1, SHA256, SHA512, MD5)
- Multi-hash - Get the performance of multi-buffer hashing with a
  single-buffer interface.
- Multi-hash + murmur - run both together.
- AES - block ciphers (XTS, GCM, CBC)
- Rolling hash - Hash input in a window which moves through the input

%package -n %{isal_crypto_devname}
Summary:	ISA-L_CRYPTO devel package
Version:        %{isal_crypto_version}
Release:        %{isal_crypto_release}%{?relval}%{?dist}
Requires:	%{isal_crypto_libname}%{?_isa} = %{isal_crypto_version}
Provides:	%{isal_crypto_libname}-static%{?_isa} = %{isal_crypto_version}

%description -n %{isal_crypto_devname}
Development files for the %{isal_crypto_libname} library.

%files -n %{isal_crypto_libname}
%{_libdir}/libisal_crypto.so.*

%files -n %{isal_crypto_devname}
%dir %{_includedir}/isa-l_crypto
%{_includedir}/isa-l_crypto.h
%{_libdir}/libisal_crypto.so
%{_libdir}/pkgconfig/libisal_crypto.pc

