%if %{with build_deps}
%package fused
Summary: DAOS build of fused
Version:       %{daos_version}
Release:       %{daos_release}

%description fused
DAOS specific fuse library

%files fused
%dir %{daos_root}/prereq/release/fused
%{daos_root}/prereq/release/fused/*

%if %{with server}

%package pmdk
Summary: DAOS build of PMDK
Version:       %{daos_version}
Release:       %{daos_release}

%description pmdk
Prebuilt DAOS PMDK dependency

%package spdk
Summary: DAOS build of spdk
Version:       %{daos_version}
Release:       %{daos_release}

%description spdk
Prebuilt DAOS SPDK dependency

%files pmdk
%dir %{daos_root}/prereq/release/pmdk
%{daos_root}/prereq/release/pmdk/*

%files spdk
%dir %{daos_root}/prereq/release/spdk
%{daos_root}/prereq/release/spdk/*

%endif

%else
BuildRequires: %{name}-fused = %{daos_version}
%if %{with server}
BuildRequires: %{name}-pmdk = %{daos_version}
BuildRequires: %{name}-spdk = %{daos_version}
%endif
%endif
