# Custom Slurm Dependency RPMs

Slurm is a DAOS test dependency, pinned in `utils/build.config`. Source builds
use the existing Git retriever and accept patches listed under
`[patch_versions]` as `slurm=...`, stored in `deps/patches/slurm/`.
Keep `slurm_version` in `utils/rpms/package_info.sh` in sync with the source tag;
increase `slurm_release` when changing patches or packaging.

## Build

Run from the DAOS repository root in the normal DAOS build environment. Install
MUNGE and hwloc development packages first (`munge-devel hwloc-devel` on EL9
and Leap, `libmunge-dev libhwloc-dev` on Ubuntu). Accounting support also requires
MariaDB client development files providing `/usr/bin/mysql_config`: `mariadb-devel`
on EL9, `libmariadb-devel` on Leap, and `libmariadb-dev libmariadb-dev-compat` on
Ubuntu. Set `INSTALL_TEST_DEPS=true` when running the image setup scripts to
include the test-only build prerequisites; they are disabled by default.
Optional plugins are detected by Slurm's configure script. Cgroup-v2 support
requires libbpf and D-Bus development packages: `libbpf-devel dbus-devel` on
EL9, `libbpf-devel dbus-1-devel` on Leap, or `libbpf-dev libdbus-1-dev` on
Ubuntu. These packages are included by the build-environment setup scripts in
`utils/scripts/`. Accounting/MySQL support is required; HDF5 and REST services
are disabled for this test-cluster build.
Rebuild any existing Slurm dependency tree previously built without MySQL support.

Install available test dependency RPMs on EL9 or Leap, then build missing
components from their pinned Git sources:

```sh
utils/build/install_deps.sh --install-range=test-deps
bash utils/build/build_test_deps.sh PREFIX="$PWD/install"
```

The build wrapper uses `--build-test-deps=only`, reuses installed components,
and exits before compiling DAOS. Ordinary dependency builds do not enable Slurm.

SCons writes `SL_SLURM_PREFIX` to `.build_vars.sh`. Slurm is configured for its
final `/usr` paths and installed via `DESTDIR` into that dependency prefix;
no root installation or daemon startup occurs during the build. The staged
`usr/` tree is flattened to match DAOS's dependency-prefix convention. For
testing directly from the build tree, supply `PluginDir` explicitly in a test
configuration; the compiled default is the RPM installation path.

Package the built tree with FPM:

```sh
utils/build/build_packages.sh --build-range=test-deps --rpm-suffix=el9 rpms
```

Use `--rpm-suffix=suse.lp155` or `--rpm-suffix=suse.lp156` for Leap. Test-only
packaging verifies its output, preserves existing ordinary dependency RPMs,
and skips Slurm when it was not built from source. The ordinary `deps` and `all`
packaging ranges do not include test dependencies.
Debian packaging is not provided by this wrapper.

## Packages

- `slurm`: clients, shared libraries, plugins and manuals.
- `slurm-slurmctld`: controller and systemd unit; also provides `slurmctld`.
- `slurm-slurmd`: compute daemon, `slurmstepd` and unit; also provides the
  `slurm-slurmmd` name currently used by DAOS provisioning.
- `slurm-slurmdbd`: database daemon, systemd unit and MySQL accounting plugin,
  requiring the matching `slurm` runtime. The test dependency installer requests
  runtime, development, daemon, and example-config packages, excluding debuginfo.
- `slurm-devel`: API headers and pkg-config metadata, requiring matching `slurm`.
- `slurm-example-configs`: configuration examples marked as configuration files.

FPM generates RPM specs internally, following the existing dependency scripts;
there is no separately maintained `.spec` file. Debug packages use the shared
FPM helper. Service hooks reload systemd but do not enable or start daemons.
Install using the system package manager and configure the cluster separately:
create a consistent Slurm service account, distribute MUNGE keys, install
`/etc/slurm/slurm.conf`, and create writable state/log directories before
starting services. The examples do not replace an existing cluster configuration.
For accounting, provision a MariaDB/MySQL server separately and install a
protected `/etc/slurm/slurmdbd.conf` (mode `0600`, owned by the service account)
with the database connection settings before starting `slurmdbd`. Its example
is packaged as `slurmdbd.conf.example`; no database credentials are supplied.
This change does not switch provisioning from distribution repositories.

## Checks

```sh
shellcheck -x utils/rpms/slurm.sh
```

An actual source build and RPM install test must run in the target distribution.

See [SchedMD's administrator guide](https://slurm.schedmd.com/quickstart_admin.html#manual_build)
and [Slurm sources](https://github.com/schedmd/slurm).