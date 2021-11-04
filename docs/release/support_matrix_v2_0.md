# DAOS Version 2.0 Support Matrix

## Hardware platform support

DAOS is primarily validated and supported on Intel x86\_64 architecture.

DAOS servers require Storage Class Memory (SCM) that is supported by
[PMDK](https://github.com/pmem/pmdk).
(SCM can be emulated by DRAM for development and testing purposes,
but this is not supported in a production environment.)
DAOS has been validated with
[Intel Optane Persistent Memory 100 Series](https://ark.intel.com/content/www/us/en/ark/products/series/190349/intel-optane-persistent-memory-100-series.html)
on 2rd gen Intel Xeon Scalable processors, and
[Intel Optane Persistent Memory 200 Series](https://ark.intel.com/content/www/us/en/ark/products/series/203877/intel-optane-persistent-memory-200-series.html)
on 3rd gen Intel Xeon Scalable processors.

DAOS clients have no specific hardware dependencies.

Some users have reported successful compilation and basic testing of DAOS
on ARM64 platforms, but those envionments are not currently supported.

## Operating System Support

The DAOS software stack is built on Linux. It is primarily validated
on [CentOS Linux](https://www.centos.org/centos-linux/)
and [openSUSE Leap](https://en.opensuse.org/openSUSE:Roadmap).
CentOS Stream and openSUSE Tumbleweed are not supported.
Other Linux distributions may be supported in the future.

Please find below the recommended Linux distributions
that have been tested with the different DAOS release tags.

| **Tag** | CentOS Linux | Rocky/Alma | openSUSE Leap | Ubuntu     |
|---------|--------------|------------|---------------|------------|
| v1.0.1  | 7.7          |            | No            | No         |
| v1.2    | 7.9          |            | No            | No         |
| v2.0    | 7.9; 8.3/8.4 | No         | 15.2          | No         |
| v2.2    | 7.9          | evaluating | 15.x          | evaluating |

Information for future releases is indicative only and may change.

## Fabric Support

DAOS relies on [libfabric](https://ofiwg.github.io/libfabric/)
for communication in the DAOS data plane.
The specific libfabric versions that are required by the different
DAOS releases are provided in RPM form for the supported Linux
distributions.
They are also defined in the build system when building from source.
It is strongly recommended to use exactly those libfabric versions.

Not all libfabric providers are supported.
DAOS Version 2.0 has been validated mainly with the `verbs` provider
for InfiniBand fabrics, and the `sockets` provider for other fabrics.
Note that the `psm2` provider for Omni-Path fabrics has known issues
when used in a DAOS context, and is not supported for production
environments.

The following table lists the
[MLNX\_OFED](https://www.mellanox.com/products/infiniband-drivers/linux/mlnx_ofed)
versions that have been validated with the libfabric verbs provider
for the different DAOS release tags.
Other MLNX\_OFED versions may work, but have not been tested.

| **Tag** | MLNX\_OFED  |
|---------|-------------|
| v1.0.1  | 5.0-1       |
| v1.2    | 5.1-x       |
| v2.0    | 5.3-1/5.4-1 |
| v2.2    | tbd         |

Information for future releases is indicative only and may change.

