# DAOS Version 2.2 Support

## Community Support and Commercial Support

Community support for DAOS is available through the
[DAOS mailing list](https://daos.groups.io/) and the
[DAOS Slack channel](https://daos-stack.slack.com/).
The [DAOS community JIRA tickets](https://daosio.atlassian.net/jira)
can be searched for known issues and possible solutions.
Community support is provided on a best-effort basis
without any guaranteed SLAs.

Starting with DAOS Version 2, the Intel DAOS engineering team
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
at the DAOS 2.2 release level.
Information for future releases is indicative only and may change.
Partner support offerings may impose further constraints, for example, if they
include DAOS support as part of a more general cluster support offering
with its release cycle.

Some members of the DAOS community have reported successful compilation
and basic testing of DAOS in other environments (for example, on ARM64
platforms, or on other Linux distributions). Those activities are highly
appreciated community contributions. However, such environments are
not currently supported in a production environment.

## Hardware platforms supported for DAOS Servers

DAOS Version 2.2 supports the x86\_64 architecture.

DAOS servers require Storage Class Memory (SCM) that is supported by
[PMDK](https://github.com/pmem/pmdk).
(SCM can be emulated by DRAM for development and testing purposes,
but this is not supported in a production environment.)
DAOS Version 2.2 has been validated with
[Intel Optane Persistent Memory 100 Series](https://ark.intel.com/content/www/us/en/ark/products/series/190349/intel-optane-persistent-memory-100-series.html)
on 2nd gen Intel Xeon Scalable processors, and with
[Intel Optane Persistent Memory 200 Series](https://ark.intel.com/content/www/us/en/ark/products/series/203877/intel-optane-persistent-memory-200-series.html)
on 3rd gen Intel Xeon Scalable processors.
For maximum performance, it is strongly recommended that all memory channels
of a DAOS server are populated with one DRAM module and one Optane PMem module.
All Optane PMem modules in a DAOS server must have the same capacity.

While not strictly required, DAOS servers typically include NVMe disks
for bulk storage, which must be supported by [SPDK](https://spdk.io/).
(Files on non-NVMe storage can emulate nVMe storage for development
and testing purposes, but this is not supported in a production environment.)
All NVMe disks managed by a single DAOS engine must have the identical capacity,
and it is strongly recommended to use identical drive models.
It is also strongly recommended that all DAOS engines in a DAOS system
have identical NVMe storage configurations.
The number of targets per DAOS engine must be identical for all DAOS engines.

DAOS Version 2.2 supports Intel Volume Management Devices (VMD) on the
DAOS servers. Enabling VMD is platform-dependent; details are provided
in the Administration Guide.

Each DAOS engine needs one high-speed network port for communication in the
DAOS data plane. DAOS Version 2.2 does not support more than one
high-speed network port per DAOS engine.
(Two DAOS engines on a 2-socket server may share a
single high-speed network port for development and testing purposes,
but this is not supported in a production environment.)
It is strongly recommended that all DAOS engines in a DAOS system use the same
model of high-speed fabric adapter.
Heterogeneous adapter population across DAOS engines has **not** been tested,
and running with such configurations may cause unexpected behavior.
Please refer to "Fabric Support" below for more details.

## Hardware platforms supported for DAOS Clients

DAOS Version 2.2 supports the x86\_64 architecture.

DAOS clients have no specific hardware dependencies.

Each DAOS client needs a network port on the same high-speed interconnect
that the DAOS servers are connected to.
Multiple high-speed network ports per DAOS client are supported.

## Operating Systems supported for DAOS Servers

The DAOS software stack is built and supported on
Linux for the x86\_64 architecture.

DAOS Version 2.2 is primarily validated
on [Rocky Linux 8.5](https://docs.rockylinux.org/release_notes/8.5/)
and [openSUSE Leap 15.3](https://en.opensuse.org/openSUSE:Roadmap).
The following subsections provide details on the Linux distributions
which DAOS Version 2.2 supports on DAOS servers.

Note that all DAOS servers in a DAOS server cluster (also called _DAOS system_)
must run the same Linux distribution. DAOS clients that access a DAOS server
cluster can run the same or different Linux distributions.

### CentOS 7 and Red Hat Enterprise Linux 7

With DAOS Version 2.2, CentOS 7.9 and RHEL 7.9 are supported on DAOS servers
with 2nd gen Intel Xeon Scalable processors (Cascade Lake).

CentOS 7.9 or RHEL 7.9 are **not** supported on DAOS servers
with 3rd gen Intel Xeon Scalable processors (Ice Lake)
or newer Intel Xeon processor generations.

Links to CentOS Linux 7 and RHEL 7 Release Notes:

* [CentOS 7.9.2009](https://wiki.centos.org/Manuals/ReleaseNotes/CentOS7.2009)
* [RHEL 7.9](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/7/html/7.9_release_notes/index)

CentOS Linux 7 will reach End Of Life (EOL) on June 30th, 2024.
Refer to the [RHEL Life Cycle](https://access.redhat.com/support/policy/updates/errata/)
description on the Red Hat support website for information on RHEL support phases.

### CentOS Linux 8

CentOS Linux 8 reached the End Of Life (EOL) on December 31st, 2021.
Consequently, DAOS Version 2.2 does not support CentOS Linux 8.
DAOS clusters that have been running CentOS Linux 8 have to be migrated to
Rocky Linux 8 or RHEL 8 to deploy DAOS Version 2.2.

### Red Hat Enterprise Linux 8

DAOS Version 2.2 supports RHEL 8.5 and RHEL 8.6.

!!! note
    Most validation of DAOS 2.2 has been done on RHEL 8.5,
    which is expected to be superseded by RHEL 8.6 by the end of May 2022.
    DAOS 2.2 support of RHEL 8.6 may therefore not be available with
    the initial DAOS 2.2.0 release: If validation issues are discovered that
    require a fix; those fixes may only be provided in a DAOS 2.2.x bugfix release.

Links to RHEL 8 Release Notes:

* [RHEL 8.5](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8/html/8.5_release_notes/index)
* [RHEL 8.6](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8/html/8.6_release_notes/index)

Refer to the [RHEL Life Cycle](https://access.redhat.com/support/policy/updates/errata/)
description on the Red Hat support website for information on RHEL support phases.

### Rocky Linux 8

DAOS Version 2.2 supports Rocky Linux 8.5 and 8.6.

!!! note
    Most validation of DAOS 2.2 has been done on the Rocky Linux 8.5 release,
    which is expected to be superseded by Rocky Linux 8.6 by the end of May 2022.
    DAOS 2.2 support of Rocky Linux 8.6 may therefore not be available with
    the initial DAOS 2.2.0 release: If validation issues are discovered that
    require a fix; those fixes may only be provided in a DAOS 2.2.x bugfix release.

Links to Rocky Linux Release Notes:

* [Rocky Linux 8.5](https://docs.rockylinux.org/release_notes/8.5/)
* [Rocky Linux 8.6](https://docs.rockylinux.org/release_notes/8.6/)

### openSUSE Leap 15

DAOS Version 2.2 is supported on openSUSE Leap 15.3.

Links to openSUSE Leap 15 Release Notes:

* [openSUSE Leap 15.3](https://doc.opensuse.org/release-notes/x86_64/openSUSE/Leap/15.3/)

### SUSE Linux Enterprise Server 15

DAOS Version 2.2 is supported on SLES 15 SP3.

Links to SLES 15 Release Notes:

* [SLES 15 SP3](https://www.suse.com/releasenotes/x86_64/SUSE-SLES/15-SP3/)

Refer to the [SLES Life Cycle](https://www.suse.com/lifecycle/)
description on the SUSE support website for information on SLES support phases.

### Unsupported Linux Distributions

DAOS does not support
openSUSE Tumbleweed,
Fedora,
CentOS Stream,
Alma Linux,
Ubuntu, or
Oracle Linux.

## Operating Systems supported for DAOS Clients

The DAOS software stack is built and supported on
Linux for the x86\_64 architecture.

In DAOS Version 2.2, the supported Linux distributions and versions for DAOS clients
are identical to those for DAOS servers. Please refer to the previous section for details.

In future DAOS releases, DAOS client support may be added for additional
Linux distributions and/or versions.

## Fabric Support

DAOS relies on [libfabric](https://ofiwg.github.io/libfabric/)
for communication in the DAOS data plane.

DAOS Version 2.2 requires at least libfabric Version 1.14. The RPM distribution
of DAOS includes libfabric RPM packages with the correct version.
It is strongly recommended to use the provided libfabric version
on all DAOS servers and all DAOS clients.

Not all libfabric providers are supported.
DAOS Version 2.2 has been validated mainly with the `verbs` provider
for InfiniBand fabrics and the `tcp` provider for other fabrics.
Future DAOS releases may add support for additional libfabric providers.

Note:
DAOS Version 2.2 has been validated and supports the `tcp` provider.
Please refer to the
[Provider Feature Matrix v1.14](https://github.com/ofiwg/libfabric/wiki/Provider-Feature-Matrix-v1.14.x)
for information on the `tcp` provider in libfabric-1.14.

Note:
The `psm2` provider for Omni-Path fabrics has known issues
when used in a DAOS context and is not supported for production
environments. Please refer to the
[Cornelis Networks presentation](https://daosio.atlassian.net/wiki/download/attachments/11015454821/12_Update_on_Omni-Path_Support_for_DAOS_DUG21_19Nov2021.pdf)
at [DUG21](https://daosio.atlassian.net/wiki/spaces/DC/pages/11015454821/DUG21)
for an outlook on future Omni-Path Express support for DAOS.
In the meantime, please use the `tcp` provider on Omni-Path fabrics.

On InfiniBand fabrics with the `verbs` provider, DAOS requires that the
[Mellanox OFED (MLNX\_OFED)](https://www.mellanox.com/products/infiniband-drivers/linux/mlnx_ofed)
software stack is installed on the DAOS servers and DAOS clients.
DAOS Version 2.2 has been validated with MLNX\_OFED Version 5.5-1 and
Version 5.6-1. Versions older than 5.4-3 are not supported by DAOS 2.2.

Links to MLNX\_OFED Release Notes:

* [MLNX\_OFED 5.4-3](https://docs.nvidia.com/networking/display/MLNXOFEDv543100/Release+Notes)
* [MLNX\_OFED 5.5-1](https://docs.nvidia.com/networking/display/MLNXOFEDv551032/Release+Notes)
* [MLNX\_OFED 5.6-1](https://docs.nvidia.com/networking/display/MLNXOFEDv561000/Release+Notes)

It is strongly recommended that all DAOS servers and all DAOS clients
run the same version of MLNX\_OFED, and that the InfiniBand adapters are
updated to the firmware levels that are included in that MLNX\_OFED
distribution.
It is also strongly recommended that the same model of
InfiniBand fabric adapter is used in all DAOS servers.
DAOS Version 2.2 has **not** been tested with heterogeneous InfiniBand
adapter configurations.
The only exception to this recommendation is the mix of single-port
and dual-port adapters of the same generation, where only one of the ports
of the dual-port adapter(s) is used by DAOS.

## DAOS Scaling

DAOS is a scale-out storage solution that is designed for extreme scale.
This section summarizes the DAOS scaling targets,, some DAOS architectural limits,
and the current testing limits of DAOS Version 2.2.

Note: Scaling characteristics depend on the properties of the high-performance
interconnect, and the libfabric provider that is used. The DAOS scaling targets
below assume a non-blocking, RDMA-capable fabric. Most scaling tests so far
have been performed on InfiniBand fabrics with the libfabric `verbs`provider.

DAOS scaling targets
(these are order of magnitude figures that indicate what the DAOS architecture
should support - see below for the scales at which DAOS 2.2 has been validated):

* DAOS client nodes in a DAOS system:   10<sup>5</sup> (hundreds of thousands)
* DAOS servers in a DAOS system:        10<sup>3</sup> (thousands)
* DAOS engines per DAOS server:         10<sup>0</sup> (less than ten)
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

DAOS Version 2.2 has been validated at the following scales:

* DAOS client nodes in a DAOS system:   256
* DAOS servers in a DAOS system:        128
* DAOS engines per DAOS server:         1, 2 and 4
* DAOS targets per DAOS engine:         4-16
* SCM storage devices per DAOS engine:  6 (Optane PMem 100), 8 (Optane PMem 200)
* NVMe storage devices per DAOS engine: 0 (PMem-only pools), 4-12
* DAOS pools in a DAOS system:          100
* DAOS containers in a DAOS pool:       100
* DAOS objects in a DAOS container:     6 billion (in mdtest benchmarks)
* Application tasks accessing a DAOS container: 3072 (using verbs)

This test coverage will be expanded in subsequent DAOS releases.
