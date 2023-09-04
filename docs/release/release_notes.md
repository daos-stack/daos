# DAOS Version 2.4 Release Notes (DRAFT)

!!! note
    This document is a DRAFT of the DAOS Version 2.4 Release Notes.
    Information in this document may change without notice before the
    release of DAOS Version 2.4.

We are pleased to announce the release of DAOS version 2.4.


## DAOS Version 2.4.0 (2023-xx-xx)

### General Support

DAOS Version 2.4.0 supports the following environments:

Architecture Support:

* DAOS 2.4.0 supports the x86\_64 architecture.

Operating System Support:

* SLES 15.4 and Leap 15.4

* EL8 (RHEL, Rocky Linux, Alma Linux):

  - EL8.6 (EUS)
  - Validation of EL8.8 is in progress

Fabric and Network Provider Support:

* libfabric support for the following fabrics and providers:

  - `ofi+tcp` on all fabrics (without RXM)
  - `ofi+tcp;ofi_rxm` on all fabrics (with RXM)
  - `ofi+verbs` on InfiniBand fabrics and RoCE (with RXM)
  - `ofi+cxi` on Slingshot fabrics (with HPE-provided libfabric)
  - `ofi+opx` on Omni-Path fabrics (Technology Preview)

* [UCX](https://docs.daos.io/v2.4/admin/ucx/) support on InfiniBand fabrics:

  - `ucx+dc_x` on InfiniBand fabrics

Storage Class Memory Support:

* DAOS Servers with 2nd gen Intel Xeon Scalable processors and
  Intel Optane Persistent Memory 100 Series.

* DAOS Servers with 3rd gen Intel Xeon Scalable processors and
  Intel Optane Persistent Memory 200 Series.

* DAOS Servers without Intel Optane Persistent Memory,
  using the _Metadata-on-SSD_ (Phase1) code path (Technology Preview)

For a complete list of supported hardware and software, refer to the
[Support Matrix](https://docs.daos.io/v2.4/release/support_matrix/).


### Key features and improvements

#### Software Version Currency

* See [above](#General-Support) for supported operating system levels.

* Libfabric and MLNX\_OFED (including UCX) have been refreshed.
  Refer to the
  [Support Matrix](https://docs.daos.io/v2.4/release/support_matrix/)
  for details.

* The `ipmctl` tool to manage Intel Optane Persistent Memory
  has been updated to Version 3 (provided by the OS distributions).

* The following prerequisite software packages that are included
  in the DAOS RPM builds have been updated:

  - Argobots has been updated to 1.1-2
  - DPDK has been updated to 21.11.2-1
  - Libfabric has been updated to 1.18.0-2 (TB8), going to 1.18.1rc1
  - Mercury has been updated to 2.3.0-1
  - Raft has been updated to 0.9.2-1.403
  - SPDK has been update to 22.01.2-3

#### New Network Providers

* UCX support on InfiniBand fabrics is now generally available
  (it was a Technology Preview in DAOS 2.2).
  Refer to [UCX](https://docs.daos.io/v2.4/admin/ucx/) for details.

* Slingshot fabrics are now supported with the `ofa+cxi`provider.

* Omni-Path Express is supported as a Technology Preview,
  using the `ofi+opx` provider.
  For production usage on Omni-Path fabrics,
  please continue to use the `ofi+tcp`provider.

#### New Features and Usability Improvements

* The `daos_server scm prepare` command now supports the creation of
  multiple SCM namespaces per CPU socket,
  using the `--scm-ns-per-socket` option.
  On DAOS servers with Intel Optane Persistent Memory modules,
  this can be used to configure multiple DAOS engines per CPU socket
  (to support multiple HPC fabric links per CPU socket).

* DAOS Version 2.4 includes a Technology Preview of the
  [Metadata-on-SSD (Phase1)](https://docs.daos.io/v2.4/admin/md-on-ssd/)
  code path to support DAOS servers without Intel Optane Persistent Memory.

* DAOS Version 2.4 includes initial support for excluding,
  draining, and reintegrating DAOS engines to/from a pool,
  using the `dmg pool {exclude|drain|reintegrate}` commands.
  Expanding a pool by adding additional DAOS engines to the pool is
  also supported, using the `dmg pool extend` command.
  Refer to
  [Pool Modifications](https://docs.daos.io/v2.4/admin/pool_operations/#pool-modifications)
  in the Administration Guide for more information.

* The default container redundancy level
  has been changed from _engine_ to _server_
  (the `rf_lvl` container property now has a value of `node (2)`).
  For DAOS systems with multiple engines per server, this will reduce
  the number of available fault domains.
  So it may be possible that wide erasure codes no longer work.
  For testing purposes, it is possible to change the redundancy level
  back to _engine_.
  For production usage, the new default is highly recommended
  as it more appropriately reflects the actual fault domains.

* The Erasure Coding implementation now uses _EC parity rotation_.
  This significantly improves EC performance,
  in particular for parallel I/O into a single shared file.

* In addition to the `libioil.so` interception library (which can
  be used to intercept POSIX data I/O calls but not metadata operations),
  DAOS Version 2.4 includes a Technology Preview of a new interception
  library `libpil4dfs.so` which can also intercept POSIX metadata calls.
  Refer to
  [this section](https://docs.daos.io/v2.4/user/filesystem/#interception-library-libpil4dfs)
  in the User Guide for more information on `libpil4dfs.so`,
  including the current limitations of this Technology Preview.

* On DAOS servers with
  [VMD](https://docs.daos.io/v2.4/admin/vmd/) enabled,
  the `dmg storage led identify` command can now be used
  to visually identify one or more NVMe SSD(s).

* DAOS Version 2.4 supports
  [Multi-user dfuse](https://docs.daos.io/v2.4/user/multi-user-dfuse/).
  This feature is particularly useful on shared nodes like login nodes:
  A single instance of the `dfuse` process can be run (as root,
  or under a non-root service userid), and all users can access
  DAOS POSIX containers through that single `dfuse`instance
  instead of starting multiple per-user `dfuse` instances.

* Several dfuse enhancements have been implemented, including
  readdir caching, interception support for streaming I/O calls,
  and the ability to fine-fune the dfuse caching behavior
  through container properties and dfuse command parameters.

#### Other notable changes

When deleting a pool that still has containers configured in it,
the `dmg pool destroy` command now needs the `--recursive` option.

In `dmg pool create` the `-p $POOL_LABEL` option is now obsolete.
Use `$POOL_LABEL` as a positional argument (without the `-p`).

The `daos container create` command no longer supports the
`-l $CONT_LABEL` option.  Use the container label as a
positional argument instead (without `-l`).


### Known Issues and limitations

Known issues from DAOS 2.2, need to be validated before DAOS 2.4 GA:

- [DAOS-11685](https://daosio.atlassian.net/browse/DAOS-11685):
  Under certain workloads with `rf=2`, a server may crash.
  There is not workaround; a fix is targeted for daos-2.2.1.

- [DAOS-11317](https://daosio.atlassian.net/browse/DAOS-11317):
  Running the Mellanox-provided `mlnxofedinstall` script to install a new version of MLNX\_OFED,
  while the `mercury-ucx` RPM is already installed, will un-install `mercury-ucx`
  (as well as mercury-ucx-debuginfo if the debuginfo RPMs are installed).
  This leaves DAOS non-functional after the MOFED update.
  Workaround: Run `{yum|dnf|zypper} install mercury-ucx [mercury-ucx-debuginfo]`
  after the MLNX\_OFED update and before starting DAOS again.

- [DAOS-8848](https://daosio.atlassian.net/browse/DAOS-8848) and
  [SPDK-2587](https://github.com/spdk/spdk/issues/2587):
  Binding and unbinding NVMe SSDs between the kernel and SPDK (using the
  `daos_server storage prepare -n [--reset]` command) can sporadically cause
  the NVMe SSDs to become inaccessible.
  Workaround: This situation can be corrected by
  running `rmmod vfio_pci; modprobe vfio_pci` and `rmmod nvme; modprobe nvme`.

- [DAOS-10215](https://daosio.atlassian.net/browse/DAOS-10215):
  For Replication and Erasure Coding (EC), in DAOS 2.2 the redundancy level (`rf_lvl`)
  is set to `1 (rank=engine)`. On servers with more than one engine per server,
  setting the redundancy level to `2 (server)` would be more appropriate,
  but the `daos cont create` command currently does not support this.
  No workaround is available at this point.

- No OPA/PSM2 support.
  Please refer to the "Fabric Support" section of the
  [Support Matrix](https://docs.daos.io/v2.4/release/support_matrix/) for details.
  No workaround is available at this point.

- [DAOS-8943](https://daosio.atlassian.net/browse/DAOS-8943):
  Premature ENOSPC error / Reclaiming free NVMe space under heavy I/O load can cause early
  out-of-space errors to be reported to applications.
  No workaround is available at this point.

### Bug fixes

The DAOS 2.4 release includes fixes for numerous defects.
For details, please refer to the Github
[release/2.4 commit history](https://github.com/daos-stack/daos/commits/release/2.4)
and the associated [Jira tickets](https://jira.daos.io/) as stated in the commit messages.


## Additional resources

Visit the [online documentation](https://docs.daos.io/v2.4/) for more
information. All DAOS project source code is maintained in the
[https://github.com/daos-stack/daos](https://github.com/daos-stack/daos) repository.
Please visit this [link](https://github.com/daos-stack/daos/blob/release/2.4/LICENSE)
for more information on the licenses.

Refer to the [System Deployment](https://docs.daos.io/v2.4/admin/deployment/)
section of the [DAOS Administration Guide](https://docs.daos.io/v2.4/admin/hardware/)
for installation details.
