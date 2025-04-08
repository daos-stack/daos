%if %{with build_deps}
%package fused
Summary: DAOS build of fused

%description fused
DAOS specific fuse library

%files fused
%dir %{daos_root}/prereq/release/fused
%{daos_root}/prereq/release/fused/*

%if %{with server}

%package spdk
Summary: DAOS build of spdk

%description spdk
Prebuilt DAOS SPDK dependency

%files spdk
%dir %{daos_root}/prereq/release/spdk
%{daos_root}/prereq/release/spdk/*

%endif

%else
BuildRequires: %{name}-fused = %{daos_version}
%if %{with server}
BuildRequires: %{name}-spdk = %{daos_version}
%endif
%endif
