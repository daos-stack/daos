# DAOS Version 2.2 Release Notes

**DRAFT** **DRAFT** **DRAFT** **DRAFT** **DRAFT**

We are pleased to announce the release of DAOS version 2.2.


## DAOS Version 2.2.0 (2022-09-xx)

### General Support

This release adds the following changes to the DAOS support matrix:

- Rocky Linux 8 support is added.
- CentOS Linux 8 support is removed.
- Support for the `libfabric/tcp` provider is added (replaces `libfabric/sockets`)
- UCX support is added (Technology Preview - not recommended in production environments)

For a complete list of supported hardware and software, refer to the
[Support Matrix](https://docs.daos.io/v2.2/release/support_matrix/).

### Key features and improvements

#### EL8 Support Transition

CentOS Linux 8 support has been removed, and has been replaced by Rocky Linux 8 support.
RHEL 8.5 is no longer supported by RedHat; RHEL 8.6 and RHEL 8.4 EUS are still supported.

#### New Network Providers

- Support for the `libfabric/tcp` provider is added.
  This replaces the `libfabric/sockets` provider that was supported with DAOS 2.0.
  The `tcp` provider achieves significantly higher performance than the `sockets` provider
  and is the recommended provider for non-RDMA networks.

- UCX support is added as a Technology Preview.
  Refer to [UCX](https://docs.daos.io/v2.2/admin/ucx/) for details.
  It is a roadmap item to fully support UCX in the DAOS 2.4 release.

#### Usability Improvements

This release adds the following usability improvements:

- Interoperability of DAOS 2.2 with DAOS 2.0.

- The required number of hugepages on DAOS servers is now auto-calculated.
  It is no longer necessary (and discouraged) to specify `nr_hugepages` in the
  `daos_server.yml` configuration file.

- Intel VMD devices are now supported in the control plane.
  Roadmap items exist for future DAOS releases to add
  support for LED management of VMD-managed NVMe disks,
  and hot-plug support for VMD-managed NVMe disks.

- The `dmg pool create` command now accepts pool size specification in percent.

- POSIX containers (DFS) now support file modification time (mtime).

### Known Issues and limitations

- [DAOS-11317](https://daosio.atlassian.net/browse/DAOS-11317)
  Running the Mellanox-provided `mlnxofedinstall` script to install a new version of MLNX\_OFED,
  while the `mercury-ucx` RPM is already installed, will un-install `mercury-ucx`
  (as well as mercury-ucx-debuginfo if the debuginfo RPMs are installed).
  This leaves DAOS non-functional after the MOFED update.
  The workaround is to run `{yum|dnf|zypper} install mercury-ucx [mercury-ucx-debuginfo]`
  after the MLNX\_OFED update and before starting DAOS again.

- Binding and unbinding NVMe SSDs between the kernel and SPDK (using the
  `daos_server storage prepare -n [--reset]` command) can sporadically cause
  the NVMe SSDs to become inaccessible. This situation can be corrected by
  running `rmmod vfio_pci; modprobe vfio_pci` and `rmmod nvme; modprobe nvme`.
  See [DAOS-8848](https://daosio.atlassian.net/browse/DAOS-8848) and the
  corresponding [SPDK ticket](https://github.com/spdk/spdk/issues/2587).

- For Replication and Erasure Coding (EC), in DAOS 2.2 the redundancy level (`rf_lvl`)
  is set to `1 (rank=engine)`. On servers with more than one engine per server,
  setting the redundancy level to `2 (server)` would be more appropriate
  but the `daos cont create` command currently does not support this
  [DAOS-10215](https://daosio.atlassian.net/browse/DAOS-10215).

- No OPA/PSM2 support.
  Please refer to the "Fabric Support" section of the
  [Support Matrix](https://docs.daos.io/v2.0/release/support_matrix/) for details.

- Premature ENOSPC error / [DAOS-8943](https://daosio.atlassian.net/browse/DAOS-8943)
  Reclaiming free NVMe space under heavy I/O load can cause early out-of-space errors
  to be reported to applications.

### Bug fixes

The DAOS 2.2 release includes fixes for numerous defects, including:

- All patches included in DAOS Version 2.0.3.
- Update PMDK to 1.12.1~rc1.
- Update SPDK to 22.01.1.
- Update mercury to 2.2.0-rc6 to grab several fixes in ucx, tcp and cxi.
- Update libfabric to 1.15.1 (same as in DAOS 2.0.3).
- Remove check for dpdk/rte\_eal.
- Handle the rare case in the control plane where a node processing a SWIM dead event
  loses leadership before the membership can be updated.
- The hwloc library provides quite a bit of information about block devices;
  make this available through the control plane. Also adds support for non-PCI devices.
- Add backward compatibility code for the enable\_vmd config parameter.
- Several dtx internal fixes.
- Fix size compatibility issue with `ds_cont_prop_cont_global_version` in rdb.
- Refine code cleaning up huge pages left behind by previous instances.
- Improve QoS on request processing in the engine when running out of DMA buffers
  (FIFO order is now guaranteed to avoid starvation).
- Improve support of compound RPCs with co-located shards.
- Add support for ucx/tcp transport to cart.
- Fix assertion failure in `pool_map_get_version()`.
- Fix mtime accounting for user set mtime.
- Add VPIC and LAMMPS applications to soak testing framework.
- Report pool global version on dmg pool list/query.
- Reject pool connection from old clients after pool upgrade.

## Additional resources

Visit the [online documentation](https://docs.daos.io/v2.2/) for more
information. All DAOS project source code is maintained in the
[https://github.com/daos-stack/daos](https://github.com/daos-stack/daos) repository.
Please visit this [link](https://github.com/daos-stack/daos/blob/master/LICENSE)
for more information on the licenses.

Refer to the [System Deployment](https://docs.daos.io/v2.2/admin/deployment/)
section of the [DAOS Administration Guide](https://docs.daos.io/v2.2/admin/hardware/)
for installation details.
