# DAOS Version 2.0 Release Notes

We are pleased to announce the release of DAOS version 2.0.


## DAOS Version 2.0.3 (2022-07-14)

### Updates in this Release

The DAOS 2.0.3 release contains the following updates on top of DAOS 2.0.2:

- The DAOS 2.0 RPMs for RHEL 8 and clones are now built on EL 8.4
  (they were previously built on EL 8.3). This has the consequence that
  DAOS 2.0.3 should not be applied to DAOS nodes running EL 8.3
  (which has reached end of support in 2021).
  Update the OS to a supported level before updating to DAOS 2.0.3.

- `libfabric` has been updated to version 1.15.1-1.
  This fixes [DAOS-9883](https://daosio.atlassian.net/browse/DAOS-9883).

- `mercury` has been updated to version 2.1.0~rc4-9,
  `raft` has been updated to version 0.9.1-1401.gc18bcb8, and
  `spdk` has been updated to version 21.07-16.
  This does not fix any specific DAOS 2.0 issues, those are minor
  updates to keep those package levels in sync with DAOS 2.2 development.

- The following DAOS 2.0 issues are addressed in DAOS 2.0.3:
```
    DAOS-11029 tse/dfs: bug fix in tse or dfs about task
    DAOS-10833 object: keep the same epoch for replicate rebuild
    DAOS-10435 IV: add update and sync epoch for pool IV.
    DAOS-10756 event: check before setting the event to READY
    DAOS-10756 event: move the ev lock to outside the comp_locked function
    DAOS-8870 pool: Allow srv_hdls in TGT_QUERY_MAP
    DAOS-10748 bio: missed error code in bulk_map_one()
    DAOS-7133 tse: refine task re-init checking
    DAOS-10414 coverity: fixes for various issues
    DAOS-10381 vos: GC to take a vos_container reference
    DAOS-10148 tse: fix TSE task buffer to account for aarch64 architecture
    DAOS-10569 client: fix build on aarch64
    DAOS-10741 java: Update Netty to Latest Version
    DAOS-10673 fixes: remove usage of na_* types
    DAOS-10675 pool: fix potential program hang
    DAOS-10567 pool: track failed pool lists
    DAOS-10530 agent: Fix NUMA node rotation
    DAOS-10194 container: delete iv entry before close cont hdl
    DAOS-10541 pool: backport compatibility check
    DAOS-9636 EC: various fixes about EC
    DAOS-10494 common: Assert macros run the condition more than once
    DAOS-10435 ec: set peer parity before checking
    DAOS-10200 vos: Avoid setting dth in TLS across yield
    DAOS-10222 control: Allow rejoin with NilRank
    DAOS-10168 control: Retry Join on leadership loss
    DAOS-10194 control: Fix forced PoolDestroy flow
    DAOS-8640 obj: bug fixes in EC degraded update, key query and aggregation
    DAOS-10202 event: private event needs lock on completion and poll
    DAOS-10249 rdb: Fix uninitialized raft node ID
    DAOS-10168 control: Don't exit monitoring loop on leadership loss
    DAOS-10131 dtx: IO forward ULT avoids holding CPU for too long time
```

### Known Issues and limitations

- Binding and unbinding NVMe SSDs between the kernel and SPDK (using the
  `daos_server storage prepare -n [--reset]` command) can sporadically cause
  the NVMe SSDs to become inaccessible. This situation can be corrected by
  running `rmmod vfio_pci; modprobe vfio_pci` and `rmmod nvme; modprobe nvme`.
  See [DAOS-8848](https://daosio.atlassian.net/browse/DAOS-8848) and the
  corresponding [SPDK ticket](https://github.com/spdk/spdk/issues/2587).

- For Replication and Erasure Coding (EC), in DAOS 2.0 the redundancy level (`rf_lvl`)
  is set to `1 (rank=engine)`. On servers with more than one engine per server,
  setting the redundancy level to `2 (server)` would be more appropriate
  but the `daos cont create` command currently does not support this
  [DAOS-10215](https://daosio.atlassian.net/browse/DAOS-10215).

- DFS POSIX containers and the `daos fs copy` do not support symlinks /
  [DAOS-9254](https://daosio.atlassian.net/browse/DAOS-9254)

- No OPA/PSM2 support.
  Please refer to the "Fabric Support" section of the
  [Support Matrix](https://docs.daos.io/v2.0/release/support_matrix/) for details.

- Premature ENOSPC error / [DAOS-8943](https://daosio.atlassian.net/browse/DAOS-8943)
  Reclaiming free NVMe space is too slow and can cause early out-of-space errors
  to be reported to applications.


## DAOS Version 2.0.2 (2022-03-21)

### Updates in this Release

The DAOS 2.0.2 release contains the following updates on top of DAOS 2.0.1:

- DAOS 2.0.2 includes fixes to the EC, VOS, MS, telemetry, dtx, object components,
  raft, VMD, as well as the test and build infrastructure.

- `mercury` has been updated from 2.1.0~rc4-3 to 2.1.0~rc4-5.
  This fixes multiple issues with `dmg pool destroy`, including
  [DAOS-9725](https://daosio.atlassian.net/browse/DAOS-9561) and
  [DAOS-9006](https://daosio.atlassian.net/browse/DAOS-9006).

- dfuse readahead caching has been disabled when write-through caching is enabled
  [DAOS-9738](https://daosio.atlassian.net/browse/DAOS-9738).

- An issue with EC aggregation has been fixed  where it was running too frequently
  and consuming CPU cycles even when EC is not used
  [DAOS-9926](https://daosio.atlassian.net/browse/DAOS-9926).

- Minor changes to the
  DFS API [DAOS-10009](https://daosio.atlassian.net/browse/DAOS-10009) and
  DAOS Java API [DAOS-9379](https://daosio.atlassian.net/browse/DAOS-9379).

- The sightings with SOAK testing in 2.0.1 have been resolved.
  See [DAOS-9725](https://daosio.atlassian.net/browse/DAOS-9725).

- The `daos_server` and `daos_agent` daemons now have a secondary group membership
  in a new `daos_daemons` Linux group
  [DAOS-6344](https://daosio.atlassian.net/browse/DAOS-6344).

- Go dependency has been updated to >= 1.17
  [DAOS-9908](https://daosio.atlassian.net/browse/DAOS-9908).

- Hadoop dependency has been updated to 3.3.2
  [DAOS-10068](https://daosio.atlassian.net/browse/DAOS-10068).

- The `spdk` version has been updated from 21.07-11 to 21.07-13 (no content changes).

### Known Issues and limitations

- For Replication and Erasure Coding (EC), in DAOS 2.0 the redundancy level (`rf_lvl`)
  is set to `1 (rank=engine)`. On servers with more than one engine per server,
  setting the redundancy level to `2 (server)` would be more appropriate
  but the `daos cont create` command currently does not support this
  [DAOS-10215](https://daosio.atlassian.net/browse/DAOS-10215).

- For some workloads, performance degradations have been observed with
  libfabric 1.14 and the `tcp` provider
  [DAOS-9883](https://daosio.atlassian.net/browse/DAOS-9883).

- DFS POSIX containers and the `daos fs copy` do not support symlinks /
  [DAOS-9254](https://daosio.atlassian.net/browse/DAOS-9254)

- No OPA/PSM2 support.
  Please refer to the "Fabric Support" section of the
  [Support Matrix](https://docs.daos.io/v2.0/release/support_matrix/) for details.

- Premature ENOSPC error / [DAOS-8943](https://daosio.atlassian.net/browse/DAOS-8943)
  Reclaiming free NVMe space is too slow and can cause early out-of-space errors
  to be reported to applications.


## DAOS Version 2.0.1 (2022-01-31)

!!! note
    DAOS version 2.0.1 does not include the latest functional and security
    updates. DAOS 2.0.2 is targeted to be released in March 2022 and will
    include additional functional and/or security updates. Customers should
    update to the latest version as it becomes available.

### Updates in this Release

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

### Known Issues and limitations

- Under heavy load, `dmg pool delete` has been observed to fail.
  In some cases the pool deletion completely fails.
  In other cases the `dmg pool delete` returns success, but the
  storage allocation of the affected pool in SCM/PMem is not released
  on one or multiple engines.
  See [DAOS-9725](https://daosio.atlassian.net/browse/DAOS-9561) and
  [DAOS-9006](https://daosio.atlassian.net/browse/DAOS-9006).

- During SOAK testing of DAOS 2.0.1,
  some test jobs have failed with DER\_CSUM(-2021) checksum errors.
  This sighting is currently being investigated to determine if this is
  caused by a client-side issue with the checksum verification code,
  triggered by a hardware media error in the testing environment, or is
  a bug in the server code that may potentially cause a data corruption.
  See [DAOS-9725](https://daosio.atlassian.net/browse/DAOS-9725).

- daos fs copy does not support symlinks / [DAOS-9254](https://daosio.atlassian.net/browse/DAOS-9254)

- No OPA/PSM2 support.
  Please refer to the "Fabric Support" section of the
  [Support Matrix](https://docs.daos.io/v2.0/release/support_matrix/) for details.

- Premature ENOSPC error / [DAOS-8943](https://daosio.atlassian.net/browse/DAOS-8943)
  Reclaiming free NVMe space is too slow and can cause early out-of-space errors
  to be reported to applications.

- Misconfiguration of certificates causes server crash at start up / [DAOS-8114](https://daosio.atlassian.net/browse/DAOS-8114)


## DAOS Version 2.0.0 (2021-12-23)

!!! note
    The DAOS version 2.0 java/hadoop DAOS connector has been updated to
    use Log4j version 2.16 and may not include the latest functional and
    security updates.
    DAOS 2.0.1 is targeted to be released in January 2022 and will
    include additional functional and/or security updates.
    Customers should update to the latest version as it becomes available.

### General Support

This release adds the following changes to the DAOS support matrix:

- Starting with DAOS Version 2.0, Commercial Level-3 Support for DAOS is
 available.
- Added support for 3rd gen Intel(r) Xeon(r) Scalable Processors
 and Intel Optane Persistent Memory 200 Series.
- CentOS Linux 8 and openSUSE Leap 15.3 support is added.

For a complete list of supported hardware and software, refer to the
[Support Matrix](https://docs.daos.io/v2.0/release/support_matrix/).

### Key features and improvements

#### Erasure code

With the 2.0 release, DAOS provides the option of Reed Solomon based EC for data
protection, supporting EC data recovery for storage target failures, and data
migration while extending the storage pool. Main sub-features of DAOS EC
include:

- Reed-Solomon based EC support for I/O.

- Versioned data aggregation for EC protected object.

- Data recovery for EC protected object.

#### Telemetry and monitoring

DAOS maintains metrics and statistics for each storage engine while the engines
are running, to provide insight into DAOS health and performance and
troubleshooting. Integration with System RAS enables proactive notification of
critical DAOS/Storage events. This data can be aggregated over all nodes in
the system by external tools (such as a time-series database) to present
overall bandwidth and other statistics. The information provided includes
bytes read and written to the engine's storage, I/O latency, I/O operations,
error events, and internal state.

#### Pool and container labels

To improve ease of use, DAOS 2.0 introduces labels (in addition to UUID) as an
option to identify and reference pools and containers.

#### Improved usability and management capabilities

DAOS 2.0 has added a number of usability and management improvements, such as
improving command structures for consistency and automated client resource
management that allows DAOS to be resilient even if clients are not.

#### Increased flexibility in object layout

The object layout has been restructured to support an arbitrary number of targets
and SSDs. This addresses a performance issue when running with a total number of
targets that is not a power of two.

#### mpifileutils integration

Tools for parallel data copy are located within mpiFileUtils. mpiFileUtils
provides an MPI-based suite of tools to handle large datasets. A DAOS backend
was written to support tools like dcp and dsync.

### Known Issues and limitations

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

### Bug fixes

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

Refer to the [System Deployment](https://docs.daos.io/v2.0/admin/deployment/)
section of the [DAOS Administration Guide](https://docs.daos.io/v2.0/admin/hardware/)
for installation details.
