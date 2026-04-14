# DAOS Version 2.8 Support Matrix

DAOS 2.8 is under active development and has not been released yet.
In the meantime, please refer to the support document for the
[latest](https://docs.daos.io/latest/release/support_matrix/)
stable DAOS release.

# **DRAFT** DAOS Version 2.8 Support Matrix

## Community Support and Commercial Support

Community support for DAOS is available through the
[DAOS mailing list](https://daos.groups.io/) and the
[DAOS Slack channel](https://daos-stack.slack.com/).
The [DAOS community JIRA tickets](https://daosio.atlassian.net/jira)
can be searched for known issues and possible solutions.
Community support is provided on a best effort basis
without any guaranteed SLAs.

Commercial support for DAOS is available from
companies offering DAOS-based solutions.
Please refer to the [DAOS Foundation landing page](https://daos.io/)
for information on the DAOS partner ecosystem.

This document describes the supported environments for Level-3 support
at the DAOS Version 2.8 level.
Partner support offerings may impose further constraints, for example if they
include DAOS support as part of a more general cluster support offering
with its own release cycle.

Some members of the DAOS community have reported successful compilation
and basic testing of DAOS in other environments (for example on ARM64
platforms, or on other Linux distributions). These activities are highly
appreciated community contributions.
However such environments currently cannot be supported by the
DAOS engineering team in a production environment.

## Hardware platforms supported for DAOS Servers

DAOS Version 2.8 supports the x86\_64 architecture.

DAOS servers require byte-addressable Storage Class Memory (SCM)
for the DAOS metadata. There are two different ways to
implement SCM in a DAOS server: Using Persistent Memory,
or using DRAM combined with logging to NVMe SSDs
(also known as _Metadata-on-SSD_).

### DAOS Servers with Persistent Memory

All DAOS versions support Intel Optane Persistent Memory (PMem)
as its SCM layer. DAOS Version 2.8 has been validated with
[Intel Optane Persistent Memory 100 Series](https://ark.intel.com/content/www/us/en/ark/products/series/190349/intel-optane-persistent-memory-100-series.html)
on 2nd gen Intel Xeon Scalable Processors, and with
[Intel Optane Persistent Memory 200 Series](https://ark.intel.com/content/www/us/en/ark/products/series/203877/intel-optane-persistent-memory-200-series.html)
on 3rd gen Intel Xeon Scalable Processors.
The Intel Optane Persistent Memory 300 Series for 4th gen Intel Xeon
Scalable Processors has been cancelled, and is not supported by DAOS.

All Optane PMem modules in a DAOS server must have the same capacity.
For maximum performance, it is strongly recommended that all memory channels
of a DAOS server are populated with one DRAM module and one Optane PMem module.

[PMDK](https://github.com/pmem/pmdk) is used as the programming interface
for SCM when using Optane Persistent Memory.

### DAOS Servers without Persistent Memory

DAOS Version 2.8 supports the _Metadata-on-SSD (phase 1)_ feature
for production environments.  This code path uses DRAM memory to hold the
DAOS metadata, and persists changes to the DAOS metadata on NVMe SSDs through
a write-ahead log (WAL) and asynchronous metadata checkpointing.

DAOS Version 2.8 also contains a technology preview of _Metadata-on-SSD
(phase 2)_, which reduces the required DRAM capacity on the servers.

More details on the Metadata-on-SSD functionality can be found in the
article [DAOS beyond Persistent Memory](https://doi.org/10.1007/978-3-031-40843-4_26)
in the _ISC High Performance 2023 International Workshops proceedings_
and in the DAOS Administration Guide.

For maximum performance, it is strongly recommended that all memory channels
of a DAOS server are populated with DRAM DIMMs.

### NVMe Storage in DAOS Servers

DAOS servers use NVMe disks for bulk storage, accesses in user space
through the [SPDK](https://spdk.io/) toolkit.
It is strongly recommended that all DAOS engines in a DAOS system
have identical NVMe storage configurations.
The number of targets (I/O threads) per DAOS engine must be identical
for all DAOS engines in a DAOS system.

!!! note For development and testing purposes, NVMe storage can be emulated
         by files in a filesystem on non-NVMe storage.
         This is not a supported configuration in a production environment.

For DAOS servers _with_ persistent memory,
all NVMe disks managed by a single DAOS engine must have identical capacity
(and it is strongly recommended to use identical drive models).

For DAOS servers _without_ persistent memory, NVMe disks can be assigned three
different roles: `data`, `meta` and `wal`. All NVMe disks that are assigned
the same set of `bdev_role`s must have identical capacity
(and it is strongly recommended to use identical drive models).
NVMe disks with different `bdev_role`s can have different capacities.
For example, a higher-endurance but lower-capacity NVMe disk model
may be assigned for the `wal` and `meta` roles.

DAOS Version 2.8 supports Intel Volume Management Devices (VMD) to manage the
NVMe disks on the DAOS servers, in particular LED control and hot-plug. Enabling
VMD is platform-dependent; details are provided in the Administration Guide.

### High-Speed Networking Cards in DAOS Servers

Each DAOS engine needs one high-speed network port for communication in the
DAOS data plane. DAOS Version 2.8 does not support more than one
high-speed network port per DAOS engine. If multiple high-speed network ports
per CPU sockets are deployed, it is recommended to run multiple engines per
CPU socket (one per high-speed network port).

!!! note It is possible that two DAOS engines on a 2-socket server share a
         single high-speed network port for development and testing purposes,
         but this configuration is not supported in a production environment.

It is strongly recommended that all DAOS engines in a DAOS system use the same
model of high-speed fabric adapter.
Heterogeneous adapter configurations across DAOS engines have **not** been
tested, and running with such configurations may cause unexpected behavior.
Please refer to "High-Speed Fabric Support" below for more details.

## Hardware platforms supported for DAOS Clients

DAOS Version 2.8 supports the x86\_64 architecture.

DAOS clients have no specific hardware dependencies.

Each DAOS client needs a network port on the same high-speed interconnect
that the DAOS servers are connected to.
Multiple high-speed network ports per DAOS client are supported.
Note that a single task/process on a DAOS client will always use a single
network port. But when multiple tasks/processes are used on a DAOS client,
then the DAOS agent will distribute the load by allocating different network
ports to different tasks/processes.

## Operating Systems supported for DAOS Servers

The DAOS software stack is built and supported on
Linux for the x86\_64 architecture.

DAOS Version 2.8 has been validated on
[Rocky Linux 9.6](https://docs.rockylinux.org/release_notes/9.6/),
[Rocky Linux 9.7](https://docs.rockylinux.org/release_notes/9.7/),
[openSUSE Leap 15.6](https://en.opensuse.org/openSUSE:Roadmap),
and [SLES 15 SP7](https://www.suse.com/releasenotes/x86_64/SUSE-SLES/15-SP7/).
(Note that an
[openSUSE Leap 15.7](https://en.opensuse.org/openSUSE:Roadmap)
release does not exist.)
The following subsections provide details on the Linux distributions
which DAOS Version 2.8 supports on DAOS servers.

All DAOS servers in a DAOS server cluster (also called _DAOS system_)
must run the same Linux distribution. DAOS clients that access a DAOS server
cluster can run the same or different Linux distributions.

### SUSE Linux Enterprise Server 15 and openSUSE Leap 15

DAOS Version 2.8.0 is supported on SLES 15 SP6, SLES 15 SP7,
and openSUSE Leap 15.6.
(An openSUSE Leap 15.7 release does not exist.)

DAOS nodes running unsupported SLES 15 or openSUSE Leap 15 levels should be
updated to a supported OS level when updating DAOS to Version 2.8.

Links to SLES 15 Release Notes:

* [SLES 15 SP6](https://www.suse.com/releasenotes/x86_64/SUSE-SLES/15-SP6/)
* [SLES 15 SP7](https://www.suse.com/releasenotes/x86_64/SUSE-SLES/15-SP7/)

Links to openSUSE Leap 15 Release Notes:

* [openSUSE Leap 15.6](https://doc.opensuse.org/release-notes/x86_64/openSUSE/Leap/15.6/)

Refer to the [SLES Life Cycle](https://www.suse.com/lifecycle/)
description on the SUSE support website for information on SLES support phases.

### Enterprise Linux 8 (EL8): RHEL 8, Rocky Linux 8, AlmaLinux 8

DAOS Version 2.8 has not been validated with EL8 (EL 8.10 was the last EL8 release).
It is recommended that EL8 environments are updated to EL9
when installing DAOS Version 2.8.

### Enterprise Linux 9 (EL9): RHEL 9, Rocky Linux 9, AlmaLinux 9

DAOS Version 2.8 is supported on EL 9.6 and EL 9.7.
Validation on EL 9.8 is planned for a future maintenance release.

Links to RHEL 9 Release Notes:

* [RHEL 9.6](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/9/html/9.6_release_notes/index)
* [RHEL 9.7](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/9/html/9.7_release_notes/index)

Links to Rocky Linux Release Notes:

* [Rocky Linux 9.6](https://docs.rockylinux.org/release_notes/9_6/)
* [Rocky Linux 9.7](https://docs.rockylinux.org/release_notes/9_7/)

Links to AlmaLinux Release Notes:

* [AlmaLinux 9.6](https://wiki.almalinux.org/release-notes/9.6.html)
* [AlmaLinux 9.7](https://wiki.almalinux.org/release-notes/9.7.html)

Refer to the
[RHEL Life Cycle](https://access.redhat.com/support/policy/updates/errata/)
description on the Red Hat support website
for information on RHEL support phases.

### Unsupported Linux Distributions

DAOS 2.8 does not support
openSUSE Tumbleweed,
Fedora,
RHEL 7,
RHEL 8 (and clones),
CentOS 7,
CentOS Linux,
CentOS Stream,
Ubuntu, or
Oracle Linux.

DAOS Version 2.8 has not yet been validated on RHEL 10 (and clones),
or on SLES/openSUSE Leap 16.

## Operating Systems supported for DAOS Clients

The DAOS software stack is built and supported on
Linux for the x86\_64 architecture.

In DAOS Version 2.8, the supported Linux distributions and versions
for DAOS clients are identical to those for DAOS servers.
Please refer to the
[previous section](#Operating-Systems-supported-for-DAOS-Servers) for details.

In future DAOS releases, DAOS client support may be added for additional
Linux distributions and/or versions.

## High-Speed Fabric Support

DAOS Version 2.8 supports both
OFI [libfabric](https://ofiwg.github.io/libfabric/)
and UCF [UCX](https://openucx.org/) for communication in the DAOS data plane.
This section describes the supported network providers and contains references
to vendor-specific information for the supported networking hardware.

### OFI libfabric

With the exception of UCX for NVIDIA InfiniBand and RoCE networks,
OFI libfabric is the recommended networking stack for DAOS.
DAOS Version 2.8.0 ships with libfabric version 1.22.0.
It is strongly recommended to use the libfabric version that is included in the
[DAOS Version 2.8 RPM repository](https://packages.daos.io/v2.8/)
on all DAOS servers and all DAOS clients.
The only exception is Slingshot, which provides its own libfabric version
as part of the Slingshot Host Stack (SHS).

Links to libfabric releases on github:

* [libfabric 1.22.0](https://github.com/ofiwg/libfabric/releases/tag/v1.22.0)

Not all libfabric core providers listed in
[fi\_provider(7)](https://ofiwg.github.io/libfabric/main/man/fi_provider.7.html)
are supported by DAOS. The following providers are supported:

* The `ofi+tcp` provider is supported on all networking hardware.
  It does not use RDMA, and on an RDMA-capable network this provider typically
  does not achieve the maximum performance of the fabric.

* The `ofi+cxi` provider is supported for RDMA communication over HPE Slingshot.

* For NVIDIA based InfiniBand and RoCE fabrics, UCX is the recommended
  networking stack (as described in the next subsection).

* The `ofi+verbs` provider can be used on fabrics that support the verbs API.
  However, its RC resource consumption will be limiting scalability.

!!! note
    Starting with libfabric 1.18.0, libfabric has support for TCP without
    `rxm`. To support this, DAOS 2.8 does not automatically add `rxm`
    to the `ofi+tcp` provider string. To use `rxm` with DAOS 2.8,
    it has to be explicitly added as `ofi+tcp;ofi_rxm`.

!!! note
    The `ofi+psm3` provider for Ethernet fabrics has not been validated with
    and is not supported by any DAOS version.

### InfiniBand and RoCE with DOCA-OFED and UCX

For [NVIDIA InfiniBand](https://www.nvidia.com/en-us/networking/products/infiniband/)
fabrics and NVIDIA-based RoCE networks,
DAOS 2.8 supports [UCX](https://openucx.org/)
which is maintained by the Unified Communication Framework (UCF) consortium.

When using UCX, DAOS requires that the NVIDIA-provided DOCA-OFED stack
is installed on the DAOS servers and DAOS clients.
DAOS has not been validated and is not supported with Linux in-box OFED.

DAOS Version 2.8 has been validated with the following DOCA-OFED and UCX Versions,
which can be downloaded from the
[https://developer.nvidia.com/doca-archive](DOCA archive) page:

* [DOCA-OFED-3.1.0](https://developer.nvidia.com/doca-3-1-0-download-archive?deployment_platform=Host-Server&deployment_package=DOCA-Host&target_os=Linux&Architecture=x86_64&Profile=doca-ofed)
  (OFED-internal-25.07-0.9.7) with UCX 1.20.0
  (Git branch 'master', revision 03898fe)

* [DOCA-OFED-3.2.0](https://developer.nvidia.com/doca-3-2-0-download-archive?deployment_platform=Host-Server&deployment_package=DOCA-Host&target_os=Linux&Architecture=x86_64&Profile=doca-ofed)
  (OFED-internal-25.10-1.2.8) with UCX 1.20.0
  (Git branch 'master', revision 03898fe)

* [DOCA-OFED-3.2.1](https://developer.nvidia.com/doca-3-2-1-download-archive?deployment_platform=Host-Server&deployment_package=DOCA-Host&target_os=Linux&Architecture=x86_64&Profile=doca-ofed)
  (OFED-internal-25.10-1.7.1) with UCX 1.20.0
  (Git branch 'master', revision 03898fe)

The recommended DOCA-OFED level for DAOS 2.8 is DOCA-OFED-3.2.1.
MLNX\_OFED and DOCA-OFED versions older than 3.1 are not supported with DAOS 2.8.

The recommended UCX fabric provider is `ucx+dc_x`.

* Earlier DAOS validation of the `ucx+dc_mlx5` provider has surfaced some
  instabilities on large InfiniBand fabrics under heavy I/O load.
  To avoid these issues, the recommendation is to use `ucx+dc_x`
  in conjunction with the `mercury-2.4.1` release that is included in
  the DAOS Version 2.8 packages.

* The `ucx+tcp`, `ucx+rc_x` and `ucx+ud_x` providers
  can be used for evaluation and testing purposes,
  but they have not been validated and are not supported with DAOS Version 2.8.

It is strongly recommended that all DAOS servers and all DAOS clients
run the same version of DOCA-OFED, and that the InfiniBand adapters are
updated to the firmware levels that are included in that DOCA-OFED
distribution.

It is also strongly recommended that the same model of
InfiniBand fabric adapter is used in all DAOS servers.
DAOS Version 2.8 has **not** been tested and is not supported with
heterogeneous InfiniBand adapter configurations on the DAOS servers.
The only exception to this recommendation is the mix of single-port
and dual-port adapters of the same generation, where only one of the ports
of the dual-port adapter(s) is used by DAOS.

### HPE Slingshot with libfabric CXI

DAOS Version 2.8 supports
[HPE Slingshot](https://www.hpe.com/us/en/compute/hpc/slingshot-interconnect.html)
fabrics with the libfabric `ofi+cxi` provider.

DAOS Version 2.8 has been validated with the following
Slingshot Host Stack (SHS) releases:

* [https://support.hpe.com/hpesc/public/swd/detail?swItemId=MTX_d064d0ca8d92462cb710b0d2a6](13.0.0 (24 Sep 2025))

* [https://support.hpe.com/hpesc/public/swd/detail?swItemId=MTX_43f240ba620b473f91dc9b5a45](13.1.0 (02 Feb 2026)),
  see also the
  [https://support.hpe.com/hpesc/public/docDisplay?docId=dp00007409en_us](HPE Slingshot Host Software Release 13.1.0)
  document.

The recommended Slingshot Host Stack (SHS) level for DAOS 2.8 is SHS 13.1.

!!! note The Slingshot Host Software (SHS) stack includes its own libfabric
         version, which gets installed into `/opt/cray/libfabric/<version>/lib64`.
         For DAOS engines and DAOS clients to use the SHS version of libfabric,
         `LD_LIBRARY_PATH` needs to be set to point to the correct SHS path.

It is strongly recommended that all DAOS servers and all DAOS clients
run the same version of SHS, and that the Slingshot adapters are
updated to the firmware levels that are included in that SHS distribution.

It is also strongly recommended that the same model of
Slingshot fabric adapter is used in all DAOS servers.
DAOS Version 2.8 has **not** been tested and is not supported with
heterogeneous Slingshot adapter configurations on the DAOS servers.


### Omni-Path with libfabric TCP or VERBS

DAOS Version 2.8 should work over
[Cornelis Omni-Path](https://www.cornelis.com/products/cn5000/family)
fabrics with the libfabric `ofi+tcp` or `ofi+verbs` provider.
For optimal verbs performance, it is recommended to enable
CN5000 bulk service optimization in the HFI1 driver (`use_bulksvc=Y`).
For optimal TCP performance, it is recommended to configure
the Omni-Path fabric manager with an MTU of 10240 byte.

The recommended CN5000 Software level is:

* [https://customercenter.cornelis.com/?product=cn5000&release=12-1%2C12-1-1](CN5000 OPX Software Version 12.1.1)

Customers interested in running DAOS in an Omni-Path environment
should contact their Cornelis representative regarding DAOS support.

!!! note
    The `ofi+psm2` and `ofi+opx` providers for Omni-Path fabrics have
    known gaps when used with DAOS and are not supported.

## DAOS Scaling

DAOS is a scale-out storage solution that is designed for extreme scale.
This section summarizes the DAOS scaling targets, some DAOS architectural
limits, and the current testing limits of DAOS Version 2.8.

Note: Scaling characteristics depend on the properties of the high-performance
interconnect, and the libfaric provider that is used. The DAOS scaling targets
below assume a non-blocking, RDMA-capable fabric. Most scaling tests so far
have been performed on Slingshot fabrics with the libfabric `cxi` provider,
and on InfiniBand fabrics with the UCX `ucx+dc_x` provider.

DAOS scaling targets
(these are order of magnitude figures that indicate what the DAOS architecture
should support - see below for the scales at which DAOS 2.8 has been validated):

* DAOS client nodes in a DAOS system:   10<sup>5</sup> (hundreds of thousands)
* DAOS servers in a DAOS system:        10<sup>3</sup> (thousands)
* DAOS engines per DAOS server:         10<sup>0</sup> (less than ten)
* DAOS engines per CPU socket:          10<sup>0</sup> (1, 2 or 4)
* DAOS targets per DAOS engine:         10<sup>1</sup> (tens)
* SCM storage devices per DAOS engine:  10<sup>1</sup> (tens)
* NVMe storage devices per DAOS engine: 10<sup>1</sup> (tens)
* DAOS pools in a DAOS system:          10<sup>2</sup> (hundreds)
* DAOS containers in a DAOS pool:       10<sup>2</sup> (hundreds)
* DAOS objects in a DAOS container:     10<sup>10</sup> (tens of billions)
* Application tasks accessing a DAOS container: 10<sup>6</sup> (millions)

Note that DAOS has an architectural limit of 2<sup>16</sup>=65536
storage targets in a DAOS system, because the number of storage targets
is encoded in 16 of the 32 "DAOS internal bits" within the
128-bit DAOS Object ID.

DAOS Version 2.8 has been validated at the following scales:

* DAOS client nodes in a DAOS system:   8000
* DAOS servers in a DAOS system:        800
* DAOS engines per DAOS server:         1, 2 and 4
* DAOS engines per CPU socket:          1 and 2
* DAOS targets per DAOS engine:         4-32
* SCM storage devices per DAOS engine:  6 (Optane PMem 100), 8 (Optane PMem 200)
* NVMe storage devices per DAOS engine: 0 (PMem-only pools), 4-12
* DAOS pools in a DAOS system:          100
* DAOS containers in a DAOS pool:       100
* DAOS objects in a DAOS container:     6 billion (in mdtest benchmarks)
* Application tasks accessing a DAOS container: 31200 (using Slingshot); 23744 (using UCX+DC\_X)

This test coverage will be expanded in subsequent DAOS releases.
