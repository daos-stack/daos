# DAOS Version 2.6 Release Notes

We are pleased to announce the release of DAOS version 2.6.

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

The DAOS 2.6 release includes fixes for numerous defects.
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
