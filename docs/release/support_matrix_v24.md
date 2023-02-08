# DAOS Version 2.4 Support


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
Please refer to the
[intel.com landing page for DAOS](https://www.intel.com/content/www/us/en/high-performance-computing/daos.html)
for information on the DAOS partner ecosystem.

This document describes the supported environments for Intel Level-3 support
at the DAOS 2.4 release level.
Information for future releases is indicative only and may change.
Partner support offerings may impose further constraints, for example if they
include DAOS support as part of a more general cluster support offering
with its own release cycle.

Some members of the DAOS community have reported successful compilation
and basic testing of DAOS in other environments (for example on ARM64
platforms, or on other Linux distributions). Those activities are highly
appreciated community contributions. However such environments are
not currently supported in a production environment.


## Hardware platforms supported for DAOS Servers

DAOS Version 2.4 supports the x86\_64 architecture.

DAOS servers require Storage Class Memory (SCM) that is supported by
[PMDK](https://github.com/pmem/pmdk).
(SCM can be emulated by DRAM for development and testing purposes,
but this is not supported in a production environment.)

DAOS Version 2.4 has been validated with
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

While not strictly required, DAOS servers typically include NVMe disks
for bulk storage, which must be supported by [SPDK](https://spdk.io/).
(NVMe storage can be emulated by files on non-NVMe storage for development
and testing purposes, but this is not supported in a production environment.)
All NVMe disks managed by a single DAOS engine must have identical capacity,
and it is strongly recommended to use identical drive models.
It is also strongly recommended that all DAOS engines in a DAOS system
have identical NVMe storage configurations.
The number of targets per DAOS engine must be identical for all DAOS engines.

DAOS Version 2.4 supports Intel Volume Management Devices (VMD) to manage the
NVMe disks on the DAOS servers. Enabling VMD is platform-dependent;
details are provided in the Administration Guide.

Each DAOS engine needs one high-speed network port for communication in the
DAOS data plane. DAOS Version 2.4 does not support more than one
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

DAOS Version 2.4 supports the x86\_64 architecture.

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

DAOS Version 2.4 is primarily validated
on [Rocky Linux 8.6](https://docs.rockylinux.org/release_notes/8.6/)
and [openSUSE Leap 15.4](https://en.opensuse.org/openSUSE:Roadmap).
The following subsections provide details on the Linux distributions
which DAOS Version 2.4 supports on DAOS servers.

Note that all DAOS servers in a DAOS server cluster (also called _DAOS system_)
must run the same Linux distribution. DAOS clients that access a DAOS server
cluster can run the same or different Linux distributions.


### SUSE Linux Enterprise Server 15 and openSUSE Leap 15

DAOS Version 2.4 is supported on SLES 15 SP4 and openSUSE Leap 15.4.

General support for SLES 15 SP3 has ended on 31-Dec-2022.
DAOS nodes running SLES 15 SP3 or openSUSE 15.3
have to be updated to 15.4 before updating DAOS to version 2.4.

Links to SLES 15 Release Notes:

* [SLES 15 SP3](https://www.suse.com/releasenotes/x86_64/SUSE-SLES/15-SP3/)
* [SLES 15 SP4](https://www.suse.com/releasenotes/x86_64/SUSE-SLES/15-SP4/)

Links to openSUSE Leap 15 Release Notes:

* [openSUSE Leap 15.3](https://doc.opensuse.org/release-notes/x86_64/openSUSE/Leap/15.3/)
* [openSUSE Leap 15.4](https://doc.opensuse.org/release-notes/x86_64/openSUSE/Leap/15.4/)

Refer to the [SLES Life Cycle](https://www.suse.com/lifecycle/)
description on the SUSE support website for information on SLES support phases.


### Enterprise Linux 8 (EL8): RHEL 8, Rocky Linux 8, AlmaLinux 8

DAOS Version 2.4 is supported on EL 8.7 and EL 8.6.

!!! note
    Most validation of DAOS 2.4 has been done on the Rocky Linux 8.6 release.

!!! note
    CentOS Linux 8 is no longer supported by DAOS 2.4 or (DAOS 2.2).
    Please install a supported EL8 operating system before deploying DAOS Version 2.4.

Links to RHEL 8 Release Notes:

* [RHEL 8.6](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8/html/8.6_release_notes/index)
* [RHEL 8.7](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8/html/8.7_release_notes/index)

Links to Rocky Linux 8 Release Notes:

* [Rocky Linux 8.6](https://docs.rockylinux.org/release_notes/8_6/)
* [Rocky Linux 8.7](https://docs.rockylinux.org/release_notes/8_7/)

Links to AlmaLinux 8 Release Notes:

* [AlmaLinux 8.6](https://wiki.almalinux.org/release-notes/8.6.html)
* [AlmaLinux 8.7](https://wiki.almalinux.org/release-notes/8.7.html)

Refer to the [RHEL Life Cycle](https://access.redhat.com/support/policy/updates/errata/)
description on the Red Hat support website for information on RHEL support phases.


### Enterprise Linux 9 (EL9): RHEL 9, Rocky Linux 9, AlmaLinux 9

DAOS Version 2.4.0 has not been validated and is not supported on EL 9.
It is a roadmap item to validate EL9 with DAOS 2.6, and potentially with DAOS 2.4.1.

Links to Rocky Linux Release Notes:

* [Rocky Linux 9.0](https://docs.rockylinux.org/release_notes/9_0/)
* [Rocky Linux 9.1](https://docs.rockylinux.org/release_notes/9_1/)

Links to AlmaLinux Release Notes:

* [AlmaLinux 9.0](https://wiki.almalinux.org/release-notes/9.0.html)
* [AlmaLinux 9.1](https://wiki.almalinux.org/release-notes/9.1.html)


### Unsupported Linux Distributions

With DAOS Version 2.4, CentOS 7 and RHEL 7 are no longer supported.
Please update your DAOS servers to a supported EL8 level before updating to DAOS 2.4.

DAOS also does not support
openSUSE Tumbleweed,
Fedora,
CentOS Stream,
Ubuntu, or
Oracle Linux.


## Operating Systems supported for DAOS Clients

The DAOS software stack is built and supported on
Linux for the x86\_64 architecture.

In DAOS Version 2.4, the supported Linux distributions and versions for DAOS clients
are identical to those for DAOS servers. Please refer to the previous section for details.

In future DAOS releases, DAOS client support may be added for additional
Linux distributions and/or versions.


## Fabric Support

DAOS relies on [libfabric](https://ofiwg.github.io/libfabric/)
for communication in the DAOS data plane.

DAOS Version 2.4 requires at least libfabric Version 1.17.0. The RPM distribution
of DAOS includes libfabric RPM packages with the correct version.
It is strongly recommended to use exactly the provided libfabric version
on all DAOS servers and all DAOS clients.

Links to libfabric releases on github (libfabric is included in the DAOS RPM builds):

* [libfabric 1.15.1](https://github.com/ofiwg/libfabric/releases/tag/v1.15.1) (May 13, 2022)
* [libfabric 1.17.0](https://github.com/ofiwg/libfabric/releases/tag/v1.17.0) (Dec 17, 2022)

Not all libfabric providers are supported.
DAOS Version 2.4 has been validated mainly with the `verbs` provider
for InfiniBand fabrics, and the `tcp` provider for other fabrics.
Future DAOS releases may add support for additional libfabric providers.

!!! note
    The `psm2` provider for Omni-Path fabrics has known issues
    when used in a DAOS context, and is not supported for production
    environments. Please refer to the
    [Cornelis Networks presentation](https://daosio.atlassian.net/wiki/download/attachments/11015454821/12_Update_on_Omni-Path_Support_for_DAOS_DUG21_19Nov2021.pdf)
    at [DUG21](https://daosio.atlassian.net/wiki/spaces/DC/pages/11015454821/DUG21)
    for an outlook on future Omni-Path Express support for DAOS.
    In the meantime, please use the `tcp` provider on Omni-Path fabrics.

On InfiniBand fabrics with the `verbs` provider, DAOS requires that the
[Mellanox OFED (MLNX\_OFED)](https://www.mellanox.com/products/infiniband-drivers/linux/mlnx_ofed)
software stack is installed on the DAOS servers and DAOS clients.
DAOS Version 2.4 has been validated with MLNX\_OFED Version 5.8-1,
and  both 5.8-1.0.1.1 and 5.8-1.1.2.1 are supported.
Versions older than 5.8-1 are not supported by DAOS 2.4.
MLNX\_OFED 5.9 has not been validated with DAOS 2.4 yet.

Links to MLNX\_OFED Release Notes:

* [MLNX\_OFED 5.8-1.0.1.1](https://docs.nvidia.com/networking/display/MLNXOFEDv581011/Release+Notes) (October 31, 2022)
* [MLNX\_OFED 5.8-1.1.2.1](https://docs.nvidia.com/networking/display/MLNXOFEDv581121LTS/Release+Notes) (December 1, 2022)
* [MLNX\_OFED 5.9-0.5.6.0](https://docs.nvidia.com/networking/display/MLNXOFEDv590560/Release+Notes) (February 2, 2022); this level no longer supportes ConnectX-3 and Connect-IB adapters

It is strongly recommended that all DAOS servers and all DAOS clients
run the same version of MLNX\_OFED, and that the InfiniBand adapters are
updated to the firmware levels that are included in that MLNX\_OFED
distribution.
It is also strongly recommended that the same model of
InfiniBand fabric adapter is used in all DAOS servers.
DAOS Version 2.4 has **not** been tested with heterogeneous InfiniBand
adapter configurations.
The only exception to this recommendation is the mix of single-port
and dual-port adapters of the same generation, where only one of the ports
of the dual-port adapter(s) is used by DAOS.

For InfiniBand fabrics, DAOS 2.4 also supports [UCX](https://openucx.org/),
which can be used as an alternative to libfabric.


## DAOS Scaling

DAOS is a scale-out storage solution that is designed for extreme scale.
This section summarizes the DAOS scaling targets, some DAOS architectural limits,
and the current testing limits of DAOS Version 2.4.

Note: Scaling characteristics depend on the properties of the high-performance
interconnect, and the libfaric provider that is used. The DAOS scaling targets
below assume a non-blocking, RDMA-capable fabric. Most scaling tests so far
have been performed on InfiniBand fabrics with the libfabric `verbs` provider.

DAOS scaling targets
(these are order of magnitude figures that indicate what the DAOS architecture
should support - see below for the scales at which DAOS 2.4 has been validated):

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

Note that DAOS has an architectural limit of 2<sup>16</sup>=65536 storage targets
in a DAOS system, because the number of storage targets is encoded in
16 of the 32 "DAOS internal bits" within the 128-bit DAOS Object ID.

DAOS Version 2.4 has been validated at the following scales:

* DAOS client nodes in a DAOS system:   256
* DAOS servers in a DAOS system:        256
* DAOS engines per DAOS server:         1, 2 and 4
* DAOS engines per CPU socket:          1 and 2
* DAOS targets per DAOS engine:         4-32
* SCM storage devices per DAOS engine:  6 (Optane PMem 100), 8 (Optane PMem 200)
* NVMe storage devices per DAOS engine: 0 (PMem-only pools), 4-12
* DAOS pools in a DAOS system:          100
* DAOS containers in a DAOS pool:       100
* DAOS objects in a DAOS container:     6 billion (in mdtest benchmarks)
* Application tasks accessing a DAOS container: 3072 (using verbs)

This test coverage will be expanded in subsequent DAOS releases.
