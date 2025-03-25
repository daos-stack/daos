# DAOS Version 2.6 Release Notes

We are pleased to announce the release of DAOS version 2.6.

## DAOS Version 2.6.3 (2025-03-26)

The DAOS 2.6.3-4 release contains the following updates on top of DAOS 2.6.2

* libfabric has been updated from 1.22.0-1 to 1.22.0-2

* PMDK (libpmem\*) has been updated from 2.1.0-2 to 2.1.0-3

* Mercury has been updated from 2.4.0-1 to 2.4.0-3

### Bug fixes and improvements

The DAOS 2.6.3-4 release includes fixes for several defects

* tbd

For details, please refer to the Github
[release/2.6 commit history](https://github.com/daos-stack/daos/commits/release/2.6)
and the associated [Jira tickets](https://jira.daos.io/) as stated in the commit messages.

## DAOS Version 2.6.2 (2024-12-10)

The DAOS 2.6.2-2 release contains the following updates on top of DAOS 2.6.1:

* Bump hadoop-common from 3.3.6 to 3.4.0

### Bug fixes and improvements

The DAOS 2.6.2-2 release includes fixes for several defects

* Add function to cycle OIDs non-sequentially and gain better object distribution

* Batched CaRT event support: when multiple engines become unavailable around
  the same time, if a pool cannot tolerate the unavailability of those engines,
  it is sometimes desired that the pool would not exclude any of the engines.
  A CaRT event delay is added by this patch, events signaling the
  unavailability of engines within the delay can be handled in one batch,
  it can give DAOS a chance to reject the pool map update and avoid unnecessary
  rebuild for massive failure.

* When a daos administrator runs dmg system exclude for a given set of engines,
  the system map version / cart primary group version will be updated.
  This change can avoid engine crash when user tries to start stopped engines
  from massinve failure like switch reboot.

* Fix a bug in IV refresh, which may cause rebuild timeout

* Fix a race between telemetry init and read, which may cause crash.

* Decrease the allocation size of DTX table and avoid failure of large
  memory allocation

* Add ability to select traffic class for SWIM context, so user can choose
  low-latency traiffic class for SWIM if the network stack can support.

* Optimization of Commit-On-Share cache, it can reduce the overhead of commit
  when the system is under heavy workload.

* Allow DAOS I/O service to release RPC input buffer before sending out reply,
  so CaRT can recycle multi-receive buffer sooner.

* Fix collective RPC bug for object with sparse layout

* Handle missing PCIe capabilities in storage query usage

* retry a few times on checksum mismatch on update RPC

For details, please refer to the Github
[release/2.6 commit history](https://github.com/daos-stack/daos/commits/release/2.6)
and the associated [Jira tickets](https://jira.daos.io/) as stated in the commit messages.

## DAOS Version 2.6.1 (2024-10-05)

The DAOS 2.6.1-3 release contains the following updates on top of DAOS 2.6.0:

* Mercury update for slingshot 11.0 host stack and other UCX provider fixes.

### Bug fixes and improvements

The DAOS 2.6.1-3 release includes fixes for several defects and a few changes
of administrator interface that can improve usability of DAOS system.

* Fix a race between MS replica stepping up as leader and engines joining the
  system, this race may cause engine join to fail.

* Fix a race in concurrent container destroy which may cause engine crash.

* Pool destroy returns explicit error instead of success if there is an
  in-progress destroy against the same pool.

* EC aggregation may cause inconsistency between data shard and parity shard,
  this has been fixed in DAOS Version 2.6.1.

* Enable pool list for clients.

* Running "daos|dmg pool query-targets" with rank argument can query all
  targets on that rank.

* Add daos health check command which allows basic system health checks from client.

* DAOS Version 2.6.0 always excludes unreachable engines reported by SWIM and schedule rebuild for
  excluded engines, this is an overreaction if massive engines are impacted by power failure or
  switch reboot because data recovery is impossible in these cases. DAOS 2.6.1 introduces a new
  environment variable to set in the server yaml file for each engine (DAOS\_POOL\_RF) to indicate the
  number of engine failures seen before stopping the changing of pool membership and completing in
  progress rebuild. It will just let all I/O and on-going rebuild block. DAOS system can finish in
  progress rebuild and be available again after bringing back impacted engines. The recommendation
  is to set this environment variable to 2.

* In DAOS Version 2.6.0, accessing faulty NVMe device returns wrong error code
  to DAOS client which can fail the application. DAOS 2.6.1 returns correct
  error code to DAOS client so the client can retry and eventually access data
  in degraded mode instead of failing the I/O.

* Pil4dfs fix to avoid deadlock with level zero library on aurora and support
  for more libc functions that were not intercepted before

For details, please refer to the Github
[release/2.6 commit history](https://github.com/daos-stack/daos/commits/release/2.6)
and the associated [Jira tickets](https://jira.daos.io/) as stated in the commit messages.


## DAOS Version 2.6.0 (2024-07-26)

### General Support

DAOS Version 2.6.0 supports the following environments:

Architecture Support:

* DAOS 2.6.0 supports the x86\_64 architecture.

Operating System Support:

* SLES/Leap 15 SP5
* EL8.8/8.9 (RHEL, Rocky Linux, Alma Linux)
* EL9.2 (RHEL, Rocky Linux, Alma Linux)

Fabric and Network Provider Support:

* libfabric support for the following fabrics and providers:

    - ofi+tcp on all fabrics (without RXM)
    - ofi+tcp;ofi\_rxm on all fabrics (with RXM)
    - ofi+verbs on InfiniBand fabrics and RoCE (with RXM)
    - ofi+cxi on Slingshot fabrics (with HPE-provided libfabric)

* [UCX](https://docs.daos.io/v2.6/admin/ucx/) support on InfiniBand fabrics:

    - ucx+ud\_x on InfiniBand fabrics
    - ucx+tcp on InfiniBand fabrics

Storage Class Memory Support (PMem and non-PMem servers):

* DAOS Servers with 2nd gen Intel Xeon Scalable processors and
  Intel Optane Persistent Memory 100 Series.
* DAOS Servers with 3rd gen Intel Xeon Scalable processors and
  Intel Optane Persistent Memory 200 Series.
* DAOS Servers without Intel Optane Persistent Memory, using the
  Metadata-on-SSD (Phase1) code path.

For a complete list of supported hardware and software, refer to the
[Support Matrix](https://docs.daos.io/v2.6/release/support_matrix/)

### Key features and improvements

#### Software Version Currency

* See [above](#General-Support) for supported operating system levels.

* Libfabric and MLNX\_OFED (including UCX) have been refreshed.
  Refer to the
  [Support Matrix](https://docs.daos.io/v2.6/release/support_matrix/)
  for details.

* The following prerequisite software packages that are included
  in the DAOS RPM builds have been updated:

    - Mercury has been updated to 2.3.1
    - Raft has been updated to 0.11.1-1.416
    - PMDK has been update to 2.0.0

#### New Features and Usability Improvements

* DAOS Version 2.6 includes the production-ready version of Metadata-on-SSD
  (Phase1) that supports DAOS servers without Persistent Memory.
  In this configuration mode, application metadata is stored on SSD and
  cached in DRAM. It is persisted to a Write Ahead Log (WAL) on write and
  eventually flushed back to SSD by a background service.

* DAOS Version 2.6 supports delayed rebuild. When delayed rebuild is set
  for a pool, rebuild is not triggered immediately when a failure happens.
  It will be started with the next pool operation from the administrator,
  for example pool reintegration. This can significantly reduce unnecessary
  data movement if the administrator can bring the failed server back
  within short time window (for example, when a server is rebooted).

* DAOS pool and container services can now detect duplicated metadata RPCs.
  It can return the execution result from the previous RPC, instead of
  running the handler again which could cause nondeterministic behavior.

* DAOS Version 2.6 supports NVMe hotplug in environments both with VMD
  and without VMD.

* In DAOS 2.4 and earlier versions, the network provider specified in
  the server YML file is an immutable property that cannot be changed
  after an engine has been formatted. In DAOS Version 2.6, administrators
  can change the fabric provider without reformatting the system.

* A checksum scrubber service is added to DAOS Version 2.6. It runs when
  the storage server is idle to limit performance impact, and it scans the
  object trees to verify data checksums and reports any detected errors.
  When a configurable threshold of checksum errors on a single pool shard
  is reached, the pool target will be evicted.

* DAOS Version 2.6 now provides a production version of `libpil4dfs`.
  The `libpil4dfs` library intercepts I/O and metadata related functions
  (unlike it's counterpart `libioil` that intercepts only IO functions).
  This library provides similar performance to applications using the POSIX
  interface than the performance of the native DFS interface.

* A Technology Preview of the catastrophic recovery functionality is
  available in DAOS Version 2.6. This feature only supports offline
  check and repair of DAOS system metadata in this version.

* DAOS client side metrics is added as a Technology Preview.
  The daos\_agent configuration file includes new parameters to control
  collection and export of per-client telemetry.
  If the `telemetry_port` option is set, then per-client telemetry will be
  published in Prometheus format for real-time sampling of client processes.

* Flat KV objects are added in this version. This object type has only one
  level key in low-level data structure. This feature can reduce metadata
  overhead of some data models built on top of DAOS, for example POSIX files.

* The DAOS client can query or punch large objects in collective mode,
  which propagagtes the RPC through a multi-level spanning tree.

* The extent allocator of DAOS now uses a bitmap to manage small block
  allocation instead of always using a b+tree. This can improve allocation
  performance and reduce metadata overhead of the extent allocator.

* DAOS Version 2.6 implements server side RPC throttling. This can prevent
  DAOS servers from running out of memory when too many clients send a high
  number of RPCs to the server simmultaneously.

#### Other notable changes

* Removed `dmg storage query device-health` in favor of
  `dmg storage list-devices --health`.

* A DAOS engine that rejoins with a different address will be rejected now.
  In previous versions, it was accepted but the new address was silently ignored.

* Removed a few legacy daos\_server config file syntax:
  `scm_class` and `bdev_class`.

* The `daos_server` stand-alone commands now
  support JSON output (via the `-j` flag).

* `dmg storage query list-devices` now outputs empty NSID for unplugged device.

* Added a command line option for read-only mounting of dfuse.
  When set, this instructs the kernel to mount read-only.

* Added a new scanning feature to the `dfs` and `daos` utilities
  to report statistics about a POSIX container.

* Added VOS garbage collection metrics.

### Known Issues and limitations

* Some `libc` functions are not intercepted by `pil4dfs`.
  While most of those functions have been added, using a function that is
  not intercepted yet can cause a client program to crash.
  For the intermediate phase until all known functions are captured,
  we introduced an environment variable that users can set when using
  the `pil4dfs` library to get file descriptors from dfuse:
  Setting `D_IL_COMPATIBLE=1` will enable that mode.
  It is expected that this mode may add a small overhead as it requires
  going through the fuse kernel to obtain the file descriptor.

* High idle CPU load can occasionally happen on the DAOS engines.
  This is still under investigation.

* I/O performance of certain transfer sizes may be lower than expected
  in some environments (combinations of Operation Systems and OFED versions).
  This is still under investigation.

### Bug fixes

The DAOS 2.6.0-3 release includes fixes for numerous defects.
For details, please refer to the Github
[release/2.6 commit history](https://github.com/daos-stack/daos/commits/release/2.6)
and the associated
[Jira tickets](https://jira.daos.io/) as stated in the commit messages.

## Additional resources

Visit the [online documentation](https://docs.daos.io/v2.6/) for more
information. All DAOS project source code is maintained in the
[https://github.com/daos-stack/daos](https://github.com/daos-stack/daos)
repository. Please visit this
[link](https://github.com/daos-stack/daos/blob/release/2.6/LICENSE)
for more information on the DAOS license.

Refer to the [System Deployment](https://docs.daos.io/v2.6/admin/deployment/)
section of the
[DAOS Administration Guide](https://docs.daos.io/v2.6/admin/hardware/)
for installation details.
