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
Ubuntu. The image setup scripts include these prerequisites.
Optional plugins are detected by Slurm's configure script;
cgroup-v2 support additionally needs libbpf and D-Bus development support as
described in the upstream administrator guide. Accounting/MySQL support is
required; HDF5 and REST services are disabled for this test-cluster build.
Rebuild any existing Slurm dependency tree previously built without MySQL support.

Build just Slurm from the pinned Git source, allowing its system prerequisites:

```sh
utils/build/build_deps.sh DEPS=slurm USE_INSTALLED=munge,hwloc PREFIX="$PWD/install"
```

SCons writes `SL_SLURM_PREFIX` to `.build_vars.sh`. Slurm is configured for its
final `/usr` paths and installed via `DESTDIR` into that dependency prefix;
no root installation or daemon startup occurs during the build. The staged
`usr/` tree is flattened to match DAOS's dependency-prefix convention. For
testing directly from the build tree, supply `PluginDir` explicitly in a test
configuration; the compiled default is the RPM installation path.

Package the built tree with FPM:

```sh
DISTRO=el9 PACKAGE_OUTPUT_DIR="$PWD/rpms/deps" bash utils/rpms/slurm.sh
utils/build/verify_packages.sh --rpm-suffix=el9 rpms/deps
```

Use `DISTRO=suse.lp155` or `suse.lp156` and the matching verification suffix for
Leap. The regular `utils/build/build_packages.sh --build-range=deps` stage also
invokes this wrapper for RPM output, skipping it when Slurm was not built.
Debian packaging is not provided by this wrapper.

## Packages

- `slurm`: clients, shared libraries, plugins and manuals.
- `slurm-slurmctld`: controller and systemd unit; also provides `slurmctld`.
- `slurm-slurmd`: compute daemon, `slurmstepd` and unit; also provides the
  `slurm-slurmmd` name currently used by DAOS provisioning.
- `slurm-slurmdbd`: database daemon, systemd unit and MySQL accounting plugin,
  requiring the matching `slurm` runtime. The prebuilt dependency installer
  requests this package alongside `slurm-devel`.
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