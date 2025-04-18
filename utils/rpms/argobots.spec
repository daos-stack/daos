%if (0%{?suse_version} >= 1315)
%define argobots_lib  libabt0
%define argobots_dev  libabt-devel
%else
%define argobots_lib  argobots
%define argobots_dev  argobots-devel
%endif

%if %{with build_deps}
%package -n %{argobots_lib}
Summary: Development files for the argobots library
Version: %{argobots_version}
Release: %{argobots_release}
Group: System Environment/Libraries
%if (0%{?suse_version} >= 1315)
Provides: argobots%{_isa} = %{argobots_version}
%endif

%description -n %{argobots_lib}
Argobots is a lightweight, low-level threading and tasking framework.
This release is an experimental version of Argobots that contains
features related to user-level threads, tasklets, and some schedulers.

%package -n %{argobots_dev}
Summary: Development files for the argobots library
Group: System Environment/Libraries
Requires: %{argobots_lib}%{?_isa} = %{argobots_version}

%description -n %{argobots_dev}
Development files for the argobots library.

%files -n %{argobots_lib}
%{_libdir}/libabt.so.*
%license deps/argobots/COPYRIGHT
%doc deps/argobots/README.md

%files -n %{argobots_dev}
%{_libdir}/libabt.so
%exclude %{_libdir}/libabt.a
%exclude %{_libdir}/libabt.la
%{_libdir}/pkgconfig/argobots.pc
%{_includedir}/abt.h
%doc deps/argobots/README.md
%else
BuildRequires: %{argobots_dev} = %{argobots_version}
%endif

