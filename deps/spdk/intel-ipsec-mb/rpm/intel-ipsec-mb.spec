# Copyright (c) 2017-2021, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of Intel Corporation nor the names of its contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Versions numbers
%global major        1
%global minor        1
%global patch        0
%global fullversion  %{major}.%{minor}.%{patch}

# GitHub properties
%global githubname   intel-ipsec-mb
%global githubver    %{major}.%{minor}
%global githubfull   %{githubname}-%{githubver}

# disable producing debuginfo for this package
%global debug_package %{nil}

Summary:            IPSEC cryptography library optimized for Intel Architecture
Name:               %{githubname}
Release:            1%{?dist}
Version:            %{fullversion}
License:            BSD
Group:              Development/Tools
ExclusiveArch:      x86_64
Source0:            https://github.com/intel/%{githubname}/archive/v%{githubver}.tar.gz#/%{githubfull}.tar.gz
URL:                https://github.com/intel/%{githubname}
BuildRequires:      make
BuildRequires:      gcc >= 4.8.3
BuildRequires:      nasm >= 2.14

%description
IPSEC cryptography library optimized for Intel Architecture

%package -n intel-ipsec-mb-devel
Summary:            IPSEC cryptography library optimized for Intel Architecture
License:            BSD
Requires:           %{name}%{?_isa} = %{version}-%{release}
Group:              Development/Tools
ExclusiveArch:      x86_64

%description -n intel-ipsec-mb-devel
IPSEC cryptography library optimized for Intel Architecture

For additional information please refer to:
https://github.com/intel/%{githubname}

%prep
%autosetup -n %{githubfull}

%if 0%{?rhel} && 0%{?rhel} < 8
%ldconfig_post

%ldconfig_postun
%endif

%build
cd lib
make EXTRA_CFLAGS='%{optflags}' %{?_smp_mflags}

%install

# Install the library
install -d %{buildroot}/%{_includedir}
install -m 0644 %{_builddir}/%{githubfull}/lib/intel-ipsec-mb.h %{buildroot}/%{_includedir}
install -d %{buildroot}/%{_libdir}
install -s -m 0755 %{_builddir}/%{githubfull}/lib/libIPSec_MB.so.%{fullversion} %{buildroot}/%{_libdir}
install -d %{buildroot}/%{_mandir}/man7
install -m 0444 lib/libipsec-mb.7 %{buildroot}/%{_mandir}/man7
install -m 0444 lib/libipsec-mb-dev.7 %{buildroot}/%{_mandir}/man7
cd %{buildroot}/%{_libdir}
ln -s libIPSec_MB.so.%{fullversion} libIPSec_MB.so.%{major}
ln -s libIPSec_MB.so.%{fullversion} libIPSec_MB.so

%files

%license LICENSE
%doc README ReleaseNotes.txt

%{_libdir}/libIPSec_MB.so.%{fullversion}
%{_libdir}/libIPSec_MB.so.%{major}

%{_mandir}/man7/libipsec-mb.7.gz

%files -n %{name}-devel
%{_includedir}/intel-ipsec-mb.h
%{_mandir}/man7/libipsec-mb-dev.7.gz
%{_libdir}/libIPSec_MB.so

%changelog
* Fri Oct 22 2021 Pablo de Lara Guarch <pablo.de.lara.guarch@intel.com> 1.1.0-1
- Update for release package v1.1

* Fri Apr 23 2021 Pablo de Lara Guarch <pablo.de.lara.guarch@intel.com> 1.0.0-1
- Update for release package v1.0

* Thu Oct 29 2020 Marcel Cornu <marcel.d.cornu@intel.com> 0.55.0-1
- Update for release package v0.55

* Tue Sep 08 2020 Marcel Cornu <marcel.d.cornu@intel.com> 0.54.0-2
- Updated to improve compliance with packaging guidelines
- Added patch to fix executable stack issue

* Thu May 14 2020 Marcel Cornu <marcel.d.cornu@intel.com> 0.54.0-1
- Update for release package v0.54.0

* Thu Sep 13 2018 Marcel Cornu <marcel.d.cornu@intel.com> 0.51-1
- Update for release package v0.51

* Mon Apr 16 2018 Tomasz Kantecki <tomasz.kantecki@intel.com> 0.49-1
- update for release package v0.49
- 01org replaced with intel in URL's
- use of new makefile 'install' target with some workarounds

* Fri Aug 11 2017 Tomasz Kantecki <tomasz.kantecki@intel.com> 0.46-1
- initial version of the package
