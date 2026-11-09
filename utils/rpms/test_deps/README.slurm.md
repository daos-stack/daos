<!--
   Copyright 2026 Hewlett Packard Enterprise Development LP

   SPDX-License-Identifier: BSD-2-Clause-Patent
-->

# Slurm Test Dependency

Slurm is a DAOS functional-test dependency built through the test-dependency
path, with its custom RPM packaging scripts under `utils/rpms/test_deps/`. Its
source revision is pinned in `utils/build.config`;
update `slurm_version` in `utils/rpms/package_info.sh` with the source tag and
increment `slurm_release` when changing the build or packaging.

## Build

Run from the DAOS repository root. Slurm requires MUNGE, hwloc, and MariaDB/MySQL
development files providing `mysql_config`. Cgroup v2 additionally requires
libbpf and D-Bus development files. To install the test-only build prerequisites
on supported build images, set `INSTALL_BUILD_TEST_DEPS=true`; they are opt-in.

On EL9 or Leap, install available Slurm RPMs, build Slurm if they are unavailable,
and package a source build with:

```sh
utils/build/install_deps.sh --install-range=test-deps
utils/build/build_deps.sh --build-range=test-deps TEST_DEPS=slurm
utils/build/build_packages.sh --build-range=test-deps --rpm-suffix=el9 rpms
```

For Leap, use `--rpm-suffix=suse.lp156`. The build wrapper selects
`TEST_DEPS=slurm`, reuses an installed Slurm when available, and builds only
missing components; it does not compile DAOS. Ordinary dependency builds do not
enable test dependencies. Packaging writes to the shared `rpms/deps` directory
without deleting ordinary dependency RPMs. Custom packaging currently supports
RPM only.

The Slurm RPMs create the standard `slurm` system account and runtime
directories, and apply the normal systemd preset and upgrade/removal lifecycle.
They do not force services to start. Functional-test setup supplies the
cluster-specific configuration and prepares MUNGE credentials and test-specific
service overrides.

See [SchedMD's administrator guide](https://slurm.schedmd.com/quickstart_admin.html#manual_build)
and [Slurm sources](https://github.com/schedmd/slurm).