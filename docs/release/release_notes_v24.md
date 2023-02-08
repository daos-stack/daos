# DAOS Version 2.3 Release Notes

DAOS 2.4 is under active development and has not been released yet.
The release is planned for late 2022.
In the meantime, please refer to the support document for the
[latest](https://docs.daos.io/latest/release/release_notes/)
stable DAOS release.

This document is a work-in-progress document for DAOS 2.3,
the development branch for DAOS 2.4.

## DAOS Version 2.4.0 (2023-xx-xx)

### General Support

This release adds the following changes to the DAOS support matrix:

- Rocky Linux 8 and Alma Linux 8 support is added.
- CentOS Linux 8 support is removed.
- Support for the `libfabric/tcp` provider is added (replaces `libfabric/sockets`)
- UCX support is added (Technology Preview - not supported in production environments)

For a complete list of supported hardware and software, refer to the
[Support Matrix](https://docs.daos.io/v2.4/release/support_matrix/).

### Key features and improvements


#### Operating System Support

CentOS Linux 7 support has been removed.

Supported EL8 (RHEL, Rocky Linux, Alma Linux) levels are EL 8.7 and RHEL 8.6 (EUS).


#### New Network Providers

- UCX support is now generally available (was a Technology Preview in DAOS 2.2).
  Refer to [UCX](https://docs.daos.io/v2.4/admin/ucx/) for details.

#### Usability Improvements

This release adds the following usability improvements:


#### Other notable changes

The default redundancy level (`rf_lvl`) has been changed from
engine to server (DAOS-xxxxx). 
For DAOS systems with multiple engines per server, this will reduce
the number of available fault domains. So it may be possible that
wide erasure codes no longer work.
For testing purposes, it is possible to change the redundancy level
back to engine. For production usage, the new default is highly
recommended as it more appropriately reflects the fault domains.

When deleting a pool that still has containers configured in it,
the `dmg pool destroy` command now needs the `--recursive` option.

The `daos container create` command no longer supports the
`-l CONT_LABEL` option.  Use the container label as a
positional argument instead (without `-l`).

In `dmg pool create` the `-p $POOL_LABEL` option is now obsolete.
Just use `$POOL_LABEL` as a positional argument without the `-p`.


### Known Issues and limitations

Known issues from DAOS 2.2, to be validated before DAOS 2.4 GA:

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
  [Support Matrix](https://docs.daos.io/v2.0/release/support_matrix/) for details.
  No workaround is available at this point.

- [DAOS-8943](https://daosio.atlassian.net/browse/DAOS-8943):
  Premature ENOSPC error / Reclaiming free NVMe space under heavy I/O load can cause early
  out-of-space errors to be reported to applications.
  No workaround is available at this point.

### Bug fixes

The DAOS 2.4 release includes fixes for numerous defects, including:

- All patches included in DAOS Version 2.2.1.
- ...

## Additional resources

Visit the [online documentation](https://docs.daos.io/v2.4/) for more
information. All DAOS project source code is maintained in the
[https://github.com/daos-stack/daos](https://github.com/daos-stack/daos) repository.
Please visit this [link](https://github.com/daos-stack/daos/blob/release/2.4/LICENSE)
for more information on the licenses.

Refer to the [System Deployment](https://docs.daos.io/v2.4/admin/deployment/)
section of the [DAOS Administration Guide](https://docs.daos.io/v2.4/admin/hardware/)
for installation details.
