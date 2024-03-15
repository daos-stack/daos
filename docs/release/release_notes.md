# DAOS Version 2.4 Release Notes

We are pleased to announce the release of DAOS version 2.4.


## DAOS Version 2.4.2 (2024-03-15)

### Updates in this Release

The DAOS 2.4.2 release is mainly a bug fix release on top of DAOS 2.4.1.

Note that due to changes in the EL8 EPEL repository, the `isa-l-2.30.0-2`,
`libisa-l-2.30.0-2`, and `libisa-l-devel-2.30.0-2` RPMs have been removed
from the DAOS packages repository.

### Bug fixes

The DAOS 2.4.2 release includes fixes for several defects.
For details, please refer to the Github
[release/2.4 commit history](https://github.com/daos-stack/daos/commits/release/2.4)
and the associated [Jira tickets](https://jira.daos.io/) as stated in the commit messages.


## DAOS Version 2.4.1 (2024-01-19)

### Updates in this Release

The DAOS 2.4.1 release contains the following updates on top of DAOS 2.4.0:

* Operating System support for SLES 15.5 and Leap 15.5.

* Operating System support for EL8.8 (RHEL, Rocky Linux, Alma Linux).

* MLNX\_OFED Version 23.04 has been validated on InfiniBand fabrics.

* The [UCX](https://docs.daos.io/v2.4/admin/ucx/) provider support on
  InfiniBand fabrics has been expanded to include `ucx+ud_x`,
  which is now the recommended provider for large InfiniBand fabrics.

* The following prerequisite software packages that are included
  in the DAOS RPM builds have been updated with DAOS 2.4.1:

    - Argobots has been updated to 1.1-3
    - DPDK has been updated to 21.11.2-2
    - Libfabric has been updated to 1.19.0-1
    - Mercury has been updated to 2.3.1-2
    - Raft has been updated to 0.10.1-2
    - SPDK has been update to 22.01.2-5

### Bug fixes

The DAOS 2.4.1 release includes fixes for several defects.
For details, please refer to the Github
[release/2.4 commit history](https://github.com/daos-stack/daos/commits/release/2.4)
and the associated [Jira tickets](https://jira.daos.io/) as stated in the commit messages.


## DAOS Version 2.4.0 (2023-09-22)

### General Support

DAOS Version 2.4.0 supports the following environments:

Architecture Support:

* DAOS 2.4.0 supports the x86\_64 architecture.

Operating System Support:

* SLES 15.4 and Leap 15.4

* EL8 (RHEL, Rocky Linux, Alma Linux):

    - EL8.6 (EUS)
    - Validation of EL8.8 is in progress.

Fabric and Network Provider Support:

* libfabric support for the following fabrics and providers:

    - `ofi+tcp` on all fabrics (without RXM)
    - `ofi+tcp;ofi_rxm` on all fabrics (with RXM)
    - `ofi+verbs` on InfiniBand fabrics and RoCE (with RXM)
    - `ofi+cxi` on Slingshot fabrics (with HPE-provided libfabric)

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

    - Argobots has been updated to 1.1-3
    - DPDK has been updated to 21.11.2-2
    - Libfabric has been updated to 1.18.1-1
    - Mercury has been updated to 2.3.1~rc1-1
    - Raft has been updated to 0.10.1-1.408
    - SPDK has been update to 22.01.2-4

#### New Network Providers

* UCX support on InfiniBand fabrics is now generally available
  (it was a Technology Preview in DAOS 2.2).
  Refer to [UCX](https://docs.daos.io/v2.4/admin/ucx/) for details.

* Slingshot fabrics are now supported with the `ofa+cxi` provider.

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

To delete a pool that still has containers configured in it,
the `dmg pool destroy` command now needs the `--recursive` option.

In `dmg pool create` the `-p $POOL_LABEL` option is now obsolete.
Use `$POOL_LABEL` as a positional argument (without the `-p`).

The `daos container create` command no longer supports the
`-l $CONT_LABEL` option.  Use the container label as a
positional argument instead (without `-l`).


### Known Issues and limitations

* [DAOS-11317](https://daosio.atlassian.net/browse/DAOS-11317):
  Running the Mellanox-provided `mlnxofedinstall` script to install a new version of MLNX\_OFED,
  while the `mercury-ucx` RPM is already installed, will un-install `mercury-ucx`
  (as well as mercury-ucx-debuginfo if the debuginfo RPMs are installed).
  This leaves DAOS non-functional after the MOFED update.
  Workaround: Run `{yum|dnf|zypper} install mercury-ucx [mercury-ucx-debuginfo]`
  after the MLNX\_OFED update and before starting DAOS again.

* No OPA/PSM2 support. For Omni-Path fabrics, please use the `ofi+tcp`provider.
  Please refer to the "Fabric Support" section of the
  [Support Matrix](https://docs.daos.io/v2.4/release/support_matrix/) for details.
  No workaround is available at this point.

* The `daos-client-tests` and `daos-server-tests` RPM packages have `golang`
  prerequisites that are newer than the version provided in EL8.
  To install those RPMs on EL8 systems, it is necessary to run
  `dnf module enable go-toolset:rhel8` to satisfy the golang requirements.

* [DAOS-13129](https://daosio.atlassian.net/browse/DAOS-13129):
  With the "Metadata-on-SSD" technology preview, sporadic checksum errors
  have been observed in 48 hours soak stress testing.
  This issue is still under investigation.


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
