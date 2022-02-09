# DAOS Version 1.2 Release Notes

We are pleased to announce the release of DAOS version 1.2, advancing on a
newly created high-performance object store that is ultimately shifting the HPC
and big data paradigm.

This release is targeted towards benchmarking, partner integration, evaluation,
and brings support for the following new features:

- Per-container ACL
- Improved Control Plane
- End-to-end Data Integrity
- Replication and Self Healing
- Offline Reintegration
- HDF5 DAOS VOL Connector
- POSIX I/O improvements
- POSIX Data Mover
- Apache Spark support
- Erasure Code (Preview)

Visit the [online documentation](https://docs.daos.io/v1.2/) for more
information. All DAOS project source code is maintained in the
[https://github.com/daos-stack/daos](https://github.com/daos-stack/daos) repository.
Please visit this [link](https://github.com/daos-stack/daos/blob/master/LICENSE)
for more information on the licenses.

## Software Installation

Reference the [Software Installation](https://docs.daos.io/v1.2/admin/installation/)
section of the [DAOS Administration Guide](https://docs.daos.io/v1.2/admin/hardware/)
for more details.

## Hardware Support

### Processor Requirements
DAOS requires a 64-bit processor architecture and is primarily developed on
Intel 64 architecture. The DAOS software and the libraries it depends on
(e.g., ISA-L, SPDK, PMDK, and DPDK) can take advantage of Intel SSE and AVX
extensions.

### Network Requirements
The DAOS network layer relies on libfabrics and supports OFI providers for
Ethernet/sockets, InfiniBand/verbs. An RDMA-capable fabric is preferred for
better performance. DAOS can support multiple rails by binding different
instances of the DAOS server to individual network cards.

The DAOS control plane provides methods for administering and managing the
DAOS servers using a secure socket layer interface. An additional out-of-band
network connecting the nodes in the DAOS service cluster is required for DAOS
administration. Management traffic between clients and servers use IP over
Fabric.

### Storage Requirements
DAOS requires each storage node to have direct access to storage-class memory
(SCM). While DAOS is primarily tested and tuned for Intel(R) Optane PMEM, the
DAOS software stack is built over the Persistent Memory Development Kit (PMDK)
and the DAX feature of the Linux operating systems as described in the
[SNIA NVM Programming Model](https://www.snia.org/sites/default/files/technical_work/final/NVMProgrammingModel_v1.2.pdf).
As a result, the open-source DAOS software stack should be able to run
transparently over any storage-class memory supported by the PMDK.

The storage node should be equipped with NVMe (non-volatile memory express)
SSDs to provide capacity. HDDs, as well as SATA and SAS SSDs, are not
supported by DAOS. Both NVMe 3D-NAND and Optane SSDs are supported.
NVMe-oF devices should theoretically work with the user-space storage stack
but have not been tested.

A ratio of 6% SCM to SSD capacity is recommended to reserve enough space
in SCM to store internal metadata (e.g., pool metadata, SSD block allocation
tracking).

For testing purposes, SCM can be emulated with DRAM by mounting a tmpfs
filesystem, and NVMe SSDs can be also emulated with DRAM or a loopback
file. Any data written using a tmpfs will not be persistent across a reboot.

## DAOS Testing

DAOS 1.2 validation efforts were focused on anticipated initial use cases.
Testing has been completed in the following areas:

- Testing has been performed on CentOS 7.9, OpenSuse 15.2, with CentOS being
  used in the majority of the test cycles. Smaller scale testing has been done
  on OpenSuse 15.2, and no testing performed on Ubuntu and MOFED 5.1x
- Testing has been conducted using Intel Xeon processors, Intel 3D NAND and
  Optane based NVMe SSDs and Optane persistent memory modules although generally
  storage density was not tested at production levels.
- DAOS uses the libfabric network abstraction layer and testing has been
  performed on a number of network specific providers, including the IB verbs,
  socket and TCP providers.  Network testing is on-going and none of
  the above providers should be considered fully tested at this time.
- All DAOS 1.2 supported functionality has been tested with an emphasis on use
  cases with positive outcomes error cases (e.g., DAOS server failure) have
  limited test cycles at this time.
- Maximum scale-out of DAOS servers during test runs was 128. The number of
  client processes was scaled up to 512.
- Soak testing with an emphasis on I/O jobs in combination with basic
  administrative actions has been run and found to be error free for periods up
  to 48 hours. As with functional testing the focus has been on positive path
  testing with failure paths and fault injection coming in a future release.


## Version 1.2 major features

### End-to-end Data Integrity

For DAOS, end-to-end means that the client will calculate and verify checksums,
providing protection for data through the entire I/O stack. During a write or
update, the DAOS Client library (libdaos.so) calculates a checksum and appends
it to the RPC message before transferred over the network. Depending on the
configuration, the DAOS Server may or may not calculate checksums to verify the
data on receipt. On a fetch, the DAOS Server will send a known good checksum
with the requested data to the DAOS Client, which will calculate checksums on
the data received and verify.


[More information](https://docs.daos.io/v1.2/overview/data_integrity/)

### Per-container ACL

Client user and group access for containers is controlled by Access Control
Lists (ACLs). Access-controlled container accesses include:

 - Opening the container for access.
 - Reading and writing data in the container.
 - Reading and writing objects.
 - Getting, setting, and listing user attributes.
 - Getting, setting, and listing snapshots.
 - Deleting the container (if the pool does not grant the user permission).
 - Getting and setting container properties.
 - Getting and modifying the container ACL.
 - Modifying the container's owner.

This is reflected in the set of supported [container permissions](https://docs.daos.io/v1.2/overview/security/#permissions).

[More information](https://docs.daos.io/v1.2/user/container/#access-control-lists)

### External Device States (via dmg)

The device states that are returned from a device query by the administrator
are dependent on both the persistently stored device state in SMD, and the
in-memory BIO device list.

 - NORMAL: A fully functional device in use by DAOS (or in setup).
 - EVICTED: A device has been manually evicted and is no longer in use by DAOS.
 - UNPLUGGED: A device previously used by DAOS is unplugged.
 - NEW: A new device is available for use by DAOS.

[More information](https://docs.daos.io/v1.2/admin/administration/#nvme-ssd-health-monitoring)

### User Interface Improvement

The DAOS dmg management utility can be used to query NVMe SSD health data, as
well as provide useful commands for NVMe SSD eviction, hot plug, and device
identification. NVMe hot plug and reintegration will not be fully supported
until DAOS 2.2 release.

 - Manually evict and NVMe SSD
 - Replace an evicted SSD with a new NVMe SSD
 - Reuse a faulty NVMe SSD (w/o reintegration)
 - Locate a healthy NVMe SSD (for VMD-enabled devices only)
 - Locate an evicted NVMe SSD (for VMD-enabled devices only)

[More information](https://docs.daos.io/v1.2/admin/administration/#nvme-ssd-eviction-and-hotplug)


### Replication and Self Healing

In DAOS, if the data is replicated with multiple copies on different storage
nodes, once one of the node fail, the data on it will be rebuilt on the other
targets. This reduces the data redundancy that would be impacted by the target
failure.

[More information]( https://docs.daos.io/v1.2/admin/pool_operations/#target-exclusion-and-self-healing)


### Offline Reintegration

In DAOS, the excluded targets can be reintegrated back into the pool by dmg pool
command, though the pool should not be connected (no I/O) during reintegration,
otherwise application failure or even data corruption might be observed, which
will be fixed in the later version. The status of the reintegration can also be
checked through dmg pool query, similar as rebuild. During reintegration, all
old data on the reintegrated target will be erased. Once the target is
reintegrated, it operates the same as normal active targets.

[More information](https://github.com/daos-stack/daos/blob/master/src/bio/README.md)


### HDF5 DAOS VOL Connector

The HDF5 Library is one of the major I/O libraries and binary file formats that
is used among the HPC community. The HDF5 library has recently been enhanced
to enable non-POSIX storage paradigms, such as object stores, enabling
non-native file formats. A new architectural layer called the HDF5 Virtual
Object Layer (VOL) was added to the library to redirect data from an HDF5
application to other types of storage using HDF5 VOL connectors.

More information:
[https://github.com/HDFGroup/vol-daos](https://github.com/HDFGroup/vol-daos)
[https://github.com/HDFGroup/vol-daos/blob/master/docs/users_guide.pdf](https://github.com/HDFGroup/vol-daos/blob/master/docs/users_guide.pdf)


### POSIX Data Mover

The Dataset Mover is a collection of multiple tools that allow users to copy
and serialize data across DAOS and POSIX file systems. There is support for data
movement across POSIX, DAOS, and HDF5 containers. There is also support for
serializing a DAOS container to an HDF5 file and deserializing back to DAOS.

[More information](https://docs.daos.io/v1.2/admin/tiering_uns/#migration-tofrom-a-posix-filesystem)

### Apache Spark

Support for Spark and Hadoop is available in this release.

[More information](https://docs.daos.io/v1.2/user/spark/)

### Erasure Code (Preview)

This release implements Reed Solomon based EC for data protection of DAOS, it
supports EC data recovery for storage target failure, and data migration while
extending the storage pool. Main features of DAOS EC include:

- Reed-Solomon based EC support for I/O. DAOS has two value types: array value
  for large streaming I/O; single value for Key-value store. DAOS EC supports
  both array value and single value. An array value always has fixed stripe
  size and cell size, whereas a single value has dynamic stripe size to provide
  storage efficiency of application data with arbitrary size. This feature also
  provides a set of predefined EC object classes as its user interface. The user
  can choose the appropriate object class for their data based on their
  requirements and restrictions. For example, capacity and bandwidth overhead,
  fault tolerance level, etc.
- Versioned data aggregation for EC protected object. The data model of DAOS is
  multi-versioned: a write operation is non-destructive, and it is stored as a
  new version if a write overlaps with old data. The DAOS EC aggregation
  service can merge versioned data protected by EC, re-encode aggregated data,
  and reclaim space by removing old data versions.
- Data recovery for EC protected object.  DAOS server failure may result in loss
  of access to part of the EC protected data. If number of failures is equal or
  less than the EC redundancy factor, DAOS client can retrieve a quorum of
  surviving cells and reconstruct the inaccessible data for application that is
  trying to read, whereas DAOS server can reconstruct lost data in the
  background and recover data redundancy to the original level.

## Document updates

All documents supporting the [https://docs.daos.io/v1.2/](https://docs.daos.io/v1.2/)
site have been refreshed for this release.
Design documents in the DAOS Source repository have also been refreshed for this release.

## Known Issues

### PMEM Registration Failures with Verbs on Recent Kernels ([DAOS-6768](https://jira.hpdd.intel.com/browse/DAOS-6768))

Memory registration failures when Leap15 + verbs;rxm + pmem combination is used
without NVMe SSD configured in the pool.
crt_bulk_create() fails with verbs level fi_mr_regv() failing with -95 error: Operation Not supported.

If PMEM backed memory is replaced by tmpfs, then bulk creation succeeds.

If FI_VERBS_USE_ODP is set, pmem-backed memory registration succeeds, however the ior test slows by 90%.

This only appears to happen on Leap15.2 and not centos 7.9.

 This will be fixed in a future kernel update.

### daos_engine crashes when querying 200 pools ([DAOS-6505](https://jira.hpdd.intel.com/browse/DAOS-6505))

 This will be fixed in a future release.

### Soak: ior -a MPIIO reports DER_INVAL with oclass RP_2GX ([DAOS-6308](https://jira.hpdd.intel.com/browse/DAOS-6308))

If failures reduce the number of available targets below the number required
for an object, writes will not be possible until the system is repaired via
reintegration, and data will be vulnerable to additional failures until that
reintegration is complete.

 This will be fixed in a future release.

### Add support for co-located (targets) shards in placement ([DAOS-5499](https://jira.hpdd.intel.com/browse/DAOS-5499))

If you have a widely striped object (or even just a "normal object" but a very
small cluster) one or more failures might mean that your objects require more
targets to place than are available on the system.

 This will be fixed in a future release.

### fio/libaio not working over dfuse with direct_io ([DAOS-5299](https://jira.hpdd.intel.com/browse/DAOS-5299))

Trying to run fio over dfuse results in EINVAL errors in normal usage.
When fio is using libaio as a backend, libaio is calling io_setup() which
works, however, io_submit() is failing with EINVAL.

Currently there is a bug where dfuse doesn't set the O_DIRECT flag.
As a workaround, adding a --disable-directio flag to dfuse and setting this for
the fio test.

This will be fixed in a future kernel update.

### The dmg storage scan may report incorrect NUMA socket ID
The ndctl package maintainers have confirmed that this is a kernel regression
that has been resolved in CentOS 8. Some DAOS nodes may not report the correct
NUMA socket ID when running a "dmg storage scan." This appears to be a
regression in the CentOS7.7 kernel rather than an ndctl issue. This is due to
different versions of ndctl provisioning different JSON namespace details
(storage scan command reads the "numa_node" field). The regression also affects
the operation of the "dmg config generate" command which isn't able to detect
correct NUMA affinity for the PMem namespaces required for the config.
The regression has been fixed in the CentOS7.9 kernel.

A fix for this is not planned.

### PSM over OPA is not fully functional
If you must evaluate DAOS on an OPA fabric, we recommend using IP over
fabric/sockets for evaluation.  For benchmarking, PSM2 over OPA offers a
better picture of DAOS performance, but the PSM2 provider is not fully
functional.

For a discussion of issues and options, contact us via the
[mailing list](https://daos.groups.io/g/daos).

### Limited bandwidth to SCM with some update/write operations
With a configuration of only Intel(R) Optane Pmem for storage in a DAOS pool
(no SSDs), a known limitation currently exists on the write/update performance
with bulk transfers to the SCM with all network providers which limits the BW
per storage server.
