# DAOS Version 2.6 Release Notes (DRAFT)

We are pleased to announce the release of DAOS version 2.6.

## DAOS Version 2.6.0 (2024-July-26)
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

    - ucx+dc\_x on InfiniBand fabrics
    - ucx+tcp on all fabrics

Storage Class Memory Support:

* DAOS Servers with 2nd gen Intel Xeon Scalable processors and
  Intel Optane Persistent Memory 100 Series.
* DAOS Servers with 3rd gen Intel Xeon Scalable processors and
  Intel Optane Persistent Memory 200 Series.

No Storage Class Memory support:

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

* DAOS Version 2.6 includes production-ready version of Metadata-on-SSD
  (Phase1) thqt can support DAOS servers without Persistent Memory.
  In this configuration mode, application metadata is stored on SSD and
  cached in DRAM, it is persisted to Write Ahead Log(WAL) on write and
  eventually flushed back to SSD by a background service.

* DAOS Version 2.6 supports delayed rebuild. When delayed rebuild is set
  for a pool, rebuild is not triggered immediately when failure happens,
  it will be merged with the next pool operation from administrator,
  for example, reintegration. It can significantly reduce unnecessary
  data movement if administrator can bring the failed server back in
  short time window.

* DAOS pool and container services can now detect duplicated metadata RPC
  from resend, it can return the execution result from previous RPC,
  instead of running the handler again which may cause nondeterministic
  behavior.

* DAOS Version 2.6 supports NVMe hotplug in both with VMD and without
  VMD environments.

* In DAOS 2.4 and earlier versions, the network provider specified in
  the server YML file is an immutable property that cannot be changed
  after an engine has been formatted. In DAOS Version 2.6, administrator
  can change fabric providers without reformatting the system.

* Checksum scrubber service is added to DAOS Version 2.6. It runs when
  the storage server is idle to limit performance impact and scans the
  object trees to verify data checksums. When a configurable threshold
  of checksum errors on a single pool shard is reached, the pool target
  will be evicted.

* DAOS Version 2.6 now providers a production version of libpil4dfs.
  Libpil4dfs intercepts IO and metadata related functions unlike it's
  coutnerpart (libioil) that intercepts only IO functions. This library 
  provides similar performance as using native DFS with POSIX interface.

* Technical Preview version of catastrophic recovery is added to DAOS in
  this version. This feature only supports offline check and repair of DAOS
  system metadata in this version.

* DAOS client side metrics is added as a technical preview in this version.
  The daos\_agent configuration file includes new parameters to control
  collection and export of per-client telemetry. If the telemetry\_port option
  is set, then per-client telemetry will be published in Prometheus format for
  real-time sampling of client processes.

* Flat KV object is added in this version, this object type only has one
  level key in low-level data mode. This feature can reduce metadata
  overhead of some data models built on top of DAOS, for example, POSIX file.

* DAOS client can query or query large object in collective mode, which
  propagagtes the RPC through a multi-level spanning tree.

* Extent allocator of DAOS uses bitmap to manage small block allocation
  instead of always using b+tree. It can improve allocation performance
  and reduce metadata overhead of extent allocator.

* DAOS Version 2.6 implements server side RPC throttling, it can prevent
  DAOS server from running into OOM when too many clients send RPC to the
  server simmultaneously.

#### Other notable changes

* Remove dmg storage query device-health in favour of list-devices --health.

* DAOS engine rejoins with different address will be rejected now, in
  previous version, it can be accepted but new address is silently ignored.

* Remove a few legacy server config file syntax: “scm\_class”, “bdev\_class”.

* Support JSON output (via -j flag) for daos\_server stand-alone commands.

* Dmg storage query list-devices outputs empty NSID for unplugged device.

* Add a command line option for read-only mounting of dfuse, when set
  instruct the kernel to mount read-only.

* Add a new scanning feature to dfs and daos utility to report statistics
  about a POSIX container.

* Add VOS garbage collection metrics.

### Known Issues and limitations

* Some libc functions are not intercepted by pil4dfs. While most of those functions
  have been added, using a function that is not intercepted yet can cause a client
  program to crash. For that intermediate phase until all known functions are captured,
  we introduced an environment variable that users can use with the pil4dfs library
  to get file descriptors from dfuse. Setting D_IL_COMPATIBLE=1 would enable that mode.
  It is expected that this mode would add a small overhead as it requires going through
  the fuse kernel to obtain the file descriptor.

... [TODO]

### Bug fixes

The DAOS 2.6 release includes fixes for numerous defects.
For details, please refer to the Github
[release/2.6 commit history](https://github.com/daos-stack/daos/commits/release/2.6)
and the associated [Jira tickets](https://jira.daos.io/) as stated in the commit messages.

## Additional resources

Visit the [online documentation](https://docs.daos.io/v2.6/) for more
information. All DAOS project source code is maintained in the
[https://github.com/daos-stack/daos](https://github.com/daos-stack/daos) repository.
Please visit this [link](https://github.com/daos-stack/daos/blob/release/2.6/LICENSE)
for more information on the licenses.

Refer to the [System Deployment](https://docs.daos.io/v2.6/admin/deployment/)
section of the [DAOS Administration Guide](https://docs.daos.io/v2.6/admin/hardware/)
for installation details.
