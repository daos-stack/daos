%if %{with server}
%if %{with build_deps}

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

%else
BuildRequires: %{name}-pmdk = %{daos_version}
BuildRequires: %{name}-spdk = %{daos_version}
%endif
%endif
