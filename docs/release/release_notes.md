# DAOS Version 2.0 Release Notes

We are pleased to announce the release of DAOS version 2.0.


# DAOS Version 2.0.1 (2022-01-28)

!!! note
    DAOS version 2.0.1 does not include the latest functional and security
    updates. DAOS 2.0.2 is targeted to be released in March 2022 and will
    include additional functional and/or security updates. Customers should
    update to the latest version as it becomes available.

## Updates in this Release

The DAOS 2.0.1 release contains the following updates on top of DAOS 2.0.0:

- DAOS 2.0.1 includes fixes to the EC, VOS and Object services,
  as well as improvements to the control system and dfuse.
  It also includes numerous updates to the test and build infrastructure.

- `log4j-core` has been updated from 2.16.0 to 2.17.1
  [DAOS-8929](https://daosio.atlassian.net/browse/DAOS-8929).

- `libfabric` has been updated from 1.14.0~rc3-2 to 1.14.0-1.
  This also fixes the DAOS 2.0.0 known limitation with MOFED > 5.4-1.0.3.0
  described in [DAOS-9376](https://daosio.atlassian.net/browse/DAOS-9376).

- `mercury` has been updated from 2.1.0~rc4-1 to 2.1.0~rc4-3.
  This fixes the high CPU utilization issue in DAOS 2.0.0
  described in [DAOS-9325](https://daosio.atlassian.net/browse/DAOS-9325)

- `spdk` has been updated from 21.07-8 to 21.07-11 (minor fixes only).


# DAOS Version 2.0.0 (2021-12-23)

!!! note
    The DAOS version 2.0 java/hadoop DAOS connector has been updated to
    use Log4j version 2.16 and may not include the latest functional and
    security updates.
    DAOS 2.0.1 is targeted to be released in January 2022 and will
    include additional functional and/or security updates.
    Customers should update to the latest version as it becomes available.

## General Support

This release adds the following changes to the DAOS support matrix:

- Starting with DAOS Version 2.0, Commercial Level-3 Support for DAOS is
 available.
- Added support for 3rd gen Intel(r) Xeon(r) Scalable Processors
 and Intel Optane Persistent Memory 200 Series.
- CentOS Linux 8 and openSUSE Leap 15.3 support is added.

For a complete list of supported hardware and software, refer to the
[Support Matrix](https://docs.daos.io/v2.0/release/support_matrix/).

## Key features and improvements

### Erasure code

With the 2.0 release, DAOS provides the option of Reed Solomon based EC for data
protection, supporting EC data recovery for storage target failures, and data
migration while extending the storage pool. Main sub-features of DAOS EC
include:

- Reed-Solomon based EC support for I/O.

- Versioned data aggregation for EC protected object.

- Data recovery for EC protected object.

### Telemetry and monitoring

DAOS maintains metrics and statistics for each storage engine while the engines
are running, to provide insight into DAOS health and performance and
troubleshooting. Integration with System RAS enables proactive notification of
critical DAOS/Storage events. This data can be aggregated over all nodes in
the system by external tools (such as a time-series database) to present
overall bandwidth and other statistics. The information provided includes
bytes read and written to the engine's storage, I/O latency, I/O operations,
error events, and internal state.

### Pool and container labels

To improve ease of use, DAOS 2.0 introduces labels (in addition to UUID) as an
option to identify and reference pools and containers.

### Improved usability and management capabilities

DAOS 2.0 has added a number of usability and management improvements, such as
improving command structures for consistency and automated client resource
management that allows DAOS to be resilient even if clients are not.

### Increased flexibility in object layout

The object layout has been restructured to support an arbitrary number of targets
and SSDs. This addresses a performance issue when running with a total number of
targets that is not a power of two.

### mpifileutils integration

Tools for parallel data copy are located within mpiFileUtils. mpiFileUtils
provides an MPI-based suite of tools to handle large datasets. A DAOS backend
was written to support tools like dcp and dsync.

## Known Issues and limitations

- Application segfault with  MOFED > 5.4-1.0.3.0 / [DAOS-9376](https://daosio.atlassian.net/browse/DAOS-9376)
  Validation of CentOS 8.5 indicates an integration issue with
  MLNX\_OFED\_LINUX-5.5-1.0.3.2 and 5.4-3.1.0.0. The same issue
  can be reproduced with CentOS 8.4 and MOFED > 5.4-1.0.3.0.

- High CPU utilization / [DAOS-9325](https://daosio.atlassian.net/browse/DAOS-9325)
  Some users have reported high CPU utilization on DAOS servers
  when the system is at rest. The problem will be resolved in the next
  bug fix release.

- daos fs copy does not support symlinks / [DAOS-9254](https://daosio.atlassian.net/browse/DAOS-9254)

- No OPA/PSM2 support.
  Please refer to the "Fabric Support" section of the
  [Support Matrix](https://docs.daos.io/v2.0/release/support_matrix/) for details.

- Premature ENOSPC error / [DAOS-8943](https://daosio.atlassian.net/browse/DAOS-8943)
  Reclaiming free NVMe space is too slow and can cause early out-of-space errors
  to be reported to applications.

- Misconfiguration of certificates causes server crash at start up / [DAOS-8114](https://daosio.atlassian.net/browse/DAOS-8114)

A complete list of known issues in v2.0 can be found [**HERE**](https://daosio.atlassian.net/issues/?jql=project%20in%20(DAOS%2C%20CART)%20AND%20type%20%3D%20bug%20AND%20statuscategory%20!%3D%20done%20AND%20affectedVersion%20!%3D%20%222.1%20Community%20Release%22%20AND%20%22Bug%20Source%22%20!%3D%20%22non-product%20bug%22%20ORDER%20BY%20priority%20DESC).

## Bug fixes

The DAOS 2.0 release includes fixes for numerous defects, including:

- DAOS 2.0 has moved to Libfabric version 1.14 and Mercury 2.1, which includes a
 number of stability and scalability fixes.
- No longer shipping DAOS tests that caused dependency conflicts with MOFED.
- DAOS 2.0 fixes a number of bugs dealing with pool and container destroy that
 could result in unremovable pools/containers.
- The interception library was not correctly intercepting mkstemp(). This has
 been resolved in the 2.0 release. [DAOS-8822](https://daosio.atlassian.net/browse/DAOS-8822)
- DAOS v2.0 resolves a number of memory leak issues in prior test builds.

A complete list of bugs resolved in v2.0 can be found [**HERE**](https://daosio.atlassian.net/issues/?jql=project%20in%20(DAOS%2C%20CART)%20AND%20type%20%3D%20bug%20AND%20statuscategory%20%3D%20done%20AND%20resolution%20in%20(fixed%2C%20Fixed-Verified%2C%20Done)%20AND%20fixversion%20%3D%20%222.0%20Community%20Release%22%20AND%20%22Bug%20Source%22%20!%3D%20%22non-product%20bug%22%20ORDER%20BY%20priority%20DESC).

## Additional resources

Visit the [online documentation](https://docs.daos.io/v2.0/) for more
information. All DAOS project source code is maintained in the
[https://github.com/daos-stack/daos](https://github.com/daos-stack/daos) repository.
Please visit this [link](https://github.com/daos-stack/daos/blob/master/LICENSE)
for more information on the licenses.

Refer to the [Software Installation](https://docs.daos.io/v2.0/admin/installation/)
section of the [DAOS Administration Guide](https://docs.daos.io/v2.0/admin/hardware/)
for installation details.
