# DAOS Version 2.6 Support


## Community Support and Commercial Support

Community support for DAOS is available through the
[DAOS mailing list](https://daos.groups.io/) and the
[DAOS Slack channel](https://daos-stack.slack.com/).
The [DAOS community JIRA tickets](https://daosio.atlassian.net/jira)
can be searched for known issues and possible solutions.
Community support is provided on a best effort basis
without any guaranteed SLAs.

The Intel DAOS engineering team
can also be contracted to provide Commercial Level-3 Support for DAOS.
Under such a support agreement, Intel partners that offer DAOS
Commercial Support to their end customers will provide the DAOS
Level-1 and Level-2 support. They can then escalate Level-2 support
tickets to the Intel Level-3 support team
through a dedicated JIRA path with well-defined SLAs.
Please refer to the [DAOS Foundation landing page](https://daos.io/)
for information on the DAOS partner ecosystem.

This document describes the supported environments for Intel Level-3 support
at the DAOS Version 2.6 level.
Information for future releases is indicative only and may change.
Partner support offerings may impose further constraints, for example if they
include DAOS support as part of a more general cluster support offering
with its own release cycle.

Some members of the DAOS community have reported successful compilation
and basic testing of DAOS in other environments (for example on ARM64
platforms, or on other Linux distributions). Those activities are highly
appreciated community contributions. However such environments are
not currently supported by Intel in a production environment.


## Hardware platforms supported for DAOS Servers

DAOS Version 2.6 supports the x86\_64 architecture.

DAOS servers require byte-addressable Storage Class Memory (SCM)
for the DAOS metadata, and there are two different ways to
implement SCM in a DAOS server: Using Persistent Memory,
or using DRAM combined with logging to NVMe SSDs
(also known as _Metadata-on-SSD_).


### DAOS Servers with Persistent Memory

All DAOS versions support Intel Optane Persistent Memory (PMem)
as its SCM layer. DAOS Version 2.6 has been validated with
[Intel Optane Persistent Memory 100 Series](https://ark.intel.com/content/www/us/en/ark/products/series/190349/intel-optane-persistent-memory-100-series.html)
on 2nd gen Intel Xeon Scalable processors, and with
[Intel Optane Persistent Memory 200 Series](https://ark.intel.com/content/www/us/en/ark/products/series/203877/intel-optane-persistent-memory-200-series.html)
on 3rd gen Intel Xeon Scalable processors.

For maximum performance, it is strongly recommended that all memory channels
of a DAOS server are populated with one DRAM module and one Optane PMem module.
All Optane PMem modules in a DAOS server must have the same capacity.

!!! note
    Note that the Intel Optane Persistent Memory 300 Series
    for 4th gen Intel Xeon Scalable processors has been cancelled,
    and is not supported by DAOS.

[PMDK](https://github.com/pmem/pmdk) is used as the programming interface
when using Optane Persistent Memory.


### DAOS Servers without Persistent Memory

DAOS Version 2.6 supports the _Metadata-on-SSD_ (phase 1) feature
for production environments.  This code path uses DRAM memory to hold the
DAOS metadata, and persists the DAOS metadata on NVMe SSDs through
a write-ahead log (WAL) and asynchronous metadata checkpointing. 

More details on the Metadata-on-SSD functionality can be found in the 
article [DAOS beyond Persistent Memory](https://doi.org/10.1007/978-3-031-40843-4_26)
in the _ISC High Performance 2023 International Workshops proceedings_
and in the DAOS Administration Guide.

For maximum performance, it is strongly recommended that all memory channels
of a DAOS server are populated.


### NVMe Storage in DAOS Servers

While not strictly required, DAOS servers typically include NVMe disks
for bulk storage, which must be supported by [SPDK](https://spdk.io/).
(NVMe storage can be emulated by files on non-NVMe storage for development
and testing purposes, but this is not supported in a production environment.)
All NVMe disks managed by a single DAOS engine must have identical capacity,
and it is strongly recommended to use identical drive models.
It is also strongly recommended that all DAOS engines in a DAOS system
have identical NVMe storage configurations.
The number of targets per DAOS engine must be identical for all DAOS engines.

DAOS Version 2.6 supports Intel Volume Management Devices (VMD) to manage the
NVMe disks on the DAOS servers. Enabling VMD is platform-dependent;
details are provided in the Administration Guide.

Each DAOS engine needs one high-speed network port for communication in the
DAOS data plane. DAOS Version 2.6 does not support more than one
high-speed network port per DAOS engine.
(It is possible that two DAOS engines on a 2-socket server share a
single high-speed network port for development and testing purposes,
but this is not supported in a production environment.)
It is strongly recommended that all DAOS engines in a DAOS system use the same
model of high-speed fabric adapter.
Heterogeneous adapter population across DAOS engines has **not** been tested,
and running with such configurations may cause unexpected behavior.
Please refer to "Fabric Support" below for more details.


## Hardware platforms supported for DAOS Clients

DAOS Version 2.6 supports the x86\_64 architecture.

DAOS clients have no specific hardware dependencies.

Each DAOS client needs a network port on the same high-speed interconnect
that the DAOS servers are connected to.
Multiple high-speed network ports per DAOS client are supported.
Note that a single task on a DAOS client will always use a single network port,
but when multiple tasks per client node are used then the DAOS agent will
distribute the load by allocating different network ports to different tasks.


## Operating Systems supported for DAOS Servers

The DAOS software stack is built and supported on
Linux for the x86\_64 architecture.

DAOS Version 2.6.0 has been primarily validated on
[Rocky Linux 8.8](https://docs.rockylinux.org/release_notes/8_8/),
[Rocky Linux 9.2](https://docs.rockylinux.org/release_notes/9.2/),
and [openSUSE Leap 15.5](https://en.opensuse.org/openSUSE:Roadmap).
The following subsections provide details on the Linux distributions
which DAOS Version 2.6 supports on DAOS servers.

Note that all DAOS servers in a DAOS server cluster (also called _DAOS system_)
must run the same Linux distribution. DAOS clients that access a DAOS server
cluster can run the same or different Linux distributions.


### SUSE Linux Enterprise Server 15 and openSUSE Leap 15

DAOS Version 2.6.0 is supported on SLES 15 SP5 and openSUSE Leap 15.5.

General support for SLES 15 SP4 has ended on 31-Dec-2023.
DAOS nodes running unsupported SLES 15 or openSUSE Leap 15 levels
have to be updated to a supported SLES 15 or openSUSE Leap 15 level
before updating DAOS to version 2.6.

Links to SLES 15 Release Notes:

* [SLES 15 SP5](https://www.suse.com/releasenotes/x86_64/SUSE-SLES/15-SP5/)

Links to openSUSE Leap 15 Release Notes:

* [openSUSE Leap 15.5](https://doc.opensuse.org/release-notes/x86_64/openSUSE/Leap/15.5/)

Refer to the [SLES Life Cycle](https://www.suse.com/lifecycle/)
description on the SUSE support website for information on SLES support phases.


### Enterprise Linux 8 (EL8): RHEL 8, Rocky Linux 8, AlmaLinux 8

DAOS Version 2.6.0 is supported on EL 8.8 with Extended Update Support (EUS) and on EL 8.10.
EL 8.9 (which is no longer supported by RedHat) has not been validated with DAOS 2.6.

Links to RHEL 8 Release Notes:

* [RHEL 8.8](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8/html/8.8_release_notes/index)
* [RHEL 8.9](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8/html/8.9_release_notes/index)
* [RHEL 8.10](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8/html/8.10_release_notes/index)

Links to Rocky Linux 8 Release Notes:

* [Rocky Linux 8.8](https://docs.rockylinux.org/release_notes/8_8/)
* [Rocky Linux 8.9](https://docs.rockylinux.org/release_notes/8_9/)
* [Rocky Linux 8.10](https://docs.rockylinux.org/release_notes/8_10/)

Links to AlmaLinux 8 Release Notes:

* [AlmaLinux 8.8](https://wiki.almalinux.org/release-notes/8.8.html)
* [AlmaLinux 8.9](https://wiki.almalinux.org/release-notes/8.9.html)
* [AlmaLinux 8.10](https://wiki.almalinux.org/release-notes/8.10.html)

Refer to the
[RHEL Life Cycle](https://access.redhat.com/support/policy/updates/errata/)
description on the Red Hat support website
for information on RHEL support phases.


### Enterprise Linux 9 (EL9): RHEL 9, Rocky Linux 9, AlmaLinux 9

DAOS Version 2.6.0 is supported on EL 9.2 with Extended Update Support (EUS) and on EL 9.4.
EL 9.3 (which is no longer supported by RedHat) has not been validated with DAOS 2.6.

Links to RHEL 9 Release Notes:

* [RHEL 9.2](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/9/html/9.2_release_notes/index)
* [RHEL 9.3](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/9/html/9.3_release_notes/index)
* [RHEL 9.4](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/9/html/9.4_release_notes/index)

Links to Rocky Linux Release Notes:

* [Rocky Linux 9.2](https://docs.rockylinux.org/release_notes/9_2/)
* [Rocky Linux 9.3](https://docs.rockylinux.org/release_notes/9_3/)
* [Rocky Linux 9.4](https://docs.rockylinux.org/release_notes/9_4/)

Links to AlmaLinux Release Notes:

* [AlmaLinux 9.2](https://wiki.almalinux.org/release-notes/9.2.html)
* [AlmaLinux 9.3](https://wiki.almalinux.org/release-notes/9.3.html)
* [AlmaLinux 9.4](https://wiki.almalinux.org/release-notes/9.4.html)


### Unsupported Linux Distributions

DAOS 2.6 does not support
openSUSE Tumbleweed,
Fedora,
RHEL 7,
CentOS 7,
CentOS Linux,
CentOS Stream,
Ubuntu, or
Oracle Linux.


## Operating Systems supported for DAOS Clients

The DAOS software stack is built and supported on
Linux for the x86\_64 architecture.

In DAOS Version 2.6, the supported Linux distributions and versions
for DAOS clients are identical to those for DAOS servers.
Please refer to the
[previous section](#Operating-Systems-supported-for-DAOS-Servers) for details.

In future DAOS releases, DAOS client support may be added for additional
Linux distributions and/or versions.


## Fabric Support

DAOS Version 2.6 supports both
OFI [libfabric](https://ofiwg.github.io/libfabric/)
and UCF [UCX](https://openucx.org/) for communication in the DAOS data plane.
This section describes the supported network providers and contains references
to vendor-specific information for the supported networking hardware.


### OFI libfabric

With the exception of UCX for InfiniBand networks,
OFI libfabric is the recommended networking stack for DAOS.
DAOS Version 2.6.0 ships with libfabric version 1.19.1
(but see below for DAOS on HPE Slingshot).
It is strongly recommended to use exactly the provided libfabric version
on all DAOS servers and all DAOS clients.

Links to libfabric releases on github (the RPM distribution of DAOS
includes libfabric RPM packages with the correct version):

* [libfabric 1.19.1](https://github.com/ofiwg/libfabric/releases/tag/v1.19.1)

Not all libfabric core providers listed in
[fi\_provider(7)](https://ofiwg.github.io/libfabric/main/man/fi_provider.7.html)
are supported by DAOS. The following providers are supported:

* The `ofi+tcp` provider is supported on all networking hardware.
  It does not use RDMA, so on an RDMA-capable network this provider typically
  does not achieve the maximum performance of the fabric.
* The `ofi+verbs` provider is supported for RDMA communication over InfiniBand
  fabrics. Note that as an alternative to libfabric, the UCX networking stack
  can be used on InfiniBand fabrics as described in the next subsection.
* The `ofi+cxi` provider is supported for RDMA communication over Slingshot.

!!! note
    Starting with libfabric 1.18.0, libfabric has support for TCP without
    `rxm`. To support this, DAOS 2.6 does not automatically add `rxm`
    to the `ofi+tcp` provider string. To use `rxm` with DAOS 2.6,
    it has to be explicitly added as `ofi+tcp;ofi_rxm`.

!!! note
    The `ofi+opx` provider for Omni-Path Express on Omni-Path fabrics
    is not yet supported with DAOS Version 2.6. It may be enabled in a
    future DAOS release when validation of the OPX provides has been
    successfully completed.

!!! note
    The `ofi+psm2` provider for Omni-Path fabrics has known issues
    when used with DAOS, and it has been removed from DAOS Version 2.6.

!!! note
    The `ofi+psm3` provider for Ethernet fabrics has not been validated with
    and is not supported by DAOS Version 2.6.


### UCF Unified Communication X (UCX)

For InfiniBand fabrics, DAOS 2.6 also supports [UCX](https://openucx.org/),
which is maintained by the Unified Communication Framework (UCF) consortium.

DAOS Version 2.6 has been validated with UCX Version 1.17.0-1
which is included in MLNX\_OFED 24.04-0.6.6.0.
Older versions of MLNX\_OFED are not supported.

* The `ucx+ud_x` provider has been validated and is supported with
  DAOS Version 2.6.0.
  It is the recommended fabric provider on large InfiniBand fabrics.

* The `ucx+dc_x` provider has been validated and is supported with
  DAOS Version 2.6.0. It does not scale as high as `ucx+ud_x`
  but may provide better performance in smaller-scale InfiniBand fabrics.

* The `ucx+tcp` provider can be used for evaluation and testing
  purposes, but it has not been fully validated with DAOS Version 2.6.


### NVIDIA/Mellanox OFED (MLNX\_OFED)

On [NVIDIA/Mellanox InfiniBand](https://www.nvidia.com/en-us/networking/products/infiniband/)
fabrics, DAOS requires that the
[Mellanox OFED (MLNX\_OFED)](https://www.mellanox.com/products/infiniband-drivers/linux/mlnx_ofed)
software stack is installed on the DAOS servers and DAOS clients.

DAOS Version 2.6 has been validated with MLNX\_OFED 24.04-0.6.6.0.
Older versions of MLNX\_OFED are not supported.

Links to MLNX\_OFED Release Notes:

* [MLNX\_OFED 23.10-0.5.5.0](https://docs.nvidia.com/networking/display/mlnxofedv23100550/release+notes) (November 7, 2023)

It is strongly recommended that all DAOS servers and all DAOS clients
run the same version of MLNX\_OFED, and that the InfiniBand adapters are
updated to the firmware levels that are included in that MLNX\_OFED
distribution.
It is also strongly recommended that the same model of
InfiniBand fabric adapter is used in all DAOS servers.
DAOS Version 2.6 has **not** been tested with heterogeneous InfiniBand
adapter configurations.
The only exception to this recommendation is the mix of single-port
and dual-port adapters of the same generation, where only one of the ports
of the dual-port adapter(s) is used by DAOS.


### HPE Slingshot

Customers using an
[HPE Slingshot](https://www.hpe.com/us/en/compute/hpc/slingshot-interconnect.html)
fabric should contact their HPE representatives for information on the
recommended HPE software stack
to use with DAOS Version 2.6 and the libfabric CXI provider.


## DAOS Scaling

DAOS is a scale-out storage solution that is designed for extreme scale.
This section summarizes the DAOS scaling targets, some DAOS architectural
limits, and the current testing limits of DAOS Version 2.6.

Note: Scaling characteristics depend on the properties of the high-performance
interconnect, and the libfaric provider that is used. The DAOS scaling targets
below assume a non-blocking, RDMA-capable fabric. Most scaling tests so far
have been performed on InfiniBand fabrics with the libfabric `verbs` provider.

DAOS scaling targets
(these are order of magnitude figures that indicate what the DAOS architecture
should support - see below for the scales at which DAOS 2.6 has been validated):

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

DAOS Version 2.6 has been validated at the following scales:

* DAOS client nodes in a DAOS system:   300
* DAOS servers in a DAOS system:        642
* DAOS engines per DAOS server:         1, 2 and 4
* DAOS engines per CPU socket:          1 and 2
* DAOS targets per DAOS engine:         4-32
* SCM storage devices per DAOS engine:  6 (Optane PMem 100), 8 (Optane PMem 200)
* NVMe storage devices per DAOS engine: 0 (PMem-only pools), 4-12
* DAOS pools in a DAOS system:          100
* DAOS containers in a DAOS pool:       100
* DAOS objects in a DAOS container:     6 billion (in mdtest benchmarks)
* Application tasks accessing a DAOS container: 31200 (using Slingshot); 23744 (using UCX/UD)

This test coverage will be expanded in subsequent DAOS releases.
