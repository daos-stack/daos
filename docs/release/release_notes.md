# DAOS Version 2.6 Release Notes

We are pleased to announce the release of DAOS version 2.6.

## DAOS Version 2.6.5 (2026-06-05)

The DAOS 2.6.5 release includes the daos-2.6.5 RPM packages and their prerequisites.
It contains the following updates on top of DAOS 2.6.4:

* Libfabric has been updated to 1.22.0-5.
* Mercury has been updated to version 2.4.1.
* PMDK has been updated to version 2.1.3.

### Bug fixes and improvements

The DAOS 2.6.5 release includes fixes and improvements in the following areas:

#### Mercury and Libfabric

* OFI/Libfabric multi-receive is now enabled for supported providers.

* The libfabric plugin for Mercury is now shipped in a separate `mercury-libfabric` RPM
  (in previous releases, it was included in the base `mercury` RPM).

#### Rebuild

* Introduce a centralized migration resource manager per target that enforces global limits on
  ULT count and DMA buffer usage across all pools, preventing overallocation during
  concurrent multi-pool rebuilds (DAOS-18192).
* Process migrated object IDs directly in main xstreams instead of routing through system
  xstreams, eliminating expensive B+ tree operations in the previous round-trip path (DAOS-17928).
* When the PS leader retries rebuild or reclaim on the same pool map version, bump the rebuild
  generation so targets can distinguish the new attempt; also abort the object scan sooner
  when rebuild is interrupted to allow faster failover (DAOS-18976).
* Delay rebuild scheduling by 5 seconds so rapid sequences of pool map updates (e.g., multi-rank
  exclude/drain) are merged into a single rebuild job instead of running serially (DAOS-18425).
* Cache the object open handle per object for the rebuild puller instead of re-opening for
  each dkey migration, saving repeated layout computation overhead (DAOS-17444).
* Make pool\_discard non-blocking by spawning a ULT and returning immediately; also throttle
  EC rebuild data consumption to one unit to avoid overwhelming targets (DAOS-18487).
* Ensure the rebuild stable epoch is propagated via IV before migration starts,
  fixing an assertion failure where mpt\_max\_eph could be zero (DAOS-18747).
* Retry rebuild data fetch indefinitely on ENOMEM instead of failing the rebuild,
  allowing the system to recover once memory pressure subsides (DAOS-18326).
* When a single object rebuild fails, abort the entire rebuild early to prevent pool destroy
  timeouts; transition the rank domain status from DOWN to DOWNOUT afterward (DAOS-17736).
* Fix use of ec\_agg\_boundary before validity check, and retry failed EC aggregation peer
  updates; also fix memory leaks of mo\_csum\_iov and mrones on error paths (DAOS-18368, DAOS-18544).
* Clear stale IV cache entries before reintegration when a target is reintegrated without
  reboot, and refine error handling in cont\_agg\_eph\_sync path (DAOS-18154).

#### Object / Erasure Coding

* When unordered conditional modifications on a DTX non-leader cause epoch conflicts
  (DER\_TX\_RESTART), ask the client to retry with a random delay instead of immediately
  restarting the DTX, reducing repeated epoch collisions (DAOS-18889).
* On DTX non-leader, detect resent RPCs for transactions already in "prepared" state after
  a leader switch, and reply directly to avoid misguiding lower-layer logic (DAOS-18785).
* After engine restart, load the latest aggregation epoch from VOS instead of resetting to
  zero, so EC aggregation skips unchanged containers; also use the persisted ec\_agg\_eph\_boundary
  as the scan start epoch and prevent bumping the epoch after reset (DAOS-18161).
* Fix two checksum bugs with non-power-of-2 chunk sizes on EC objects: align checksum
  computation with VOS-space offsets during rebuild migration, and widen extent offset types
  to uint64\_t to preserve the parity indicator bit in aggregation verification (DAOS-18524).
* Yield the CPU more frequently during EC aggregation under space pressure to avoid holding
  the xstream for too long; also increase RPC retry latency and send DTX RPCs from the
  current ULT to reduce congestion from resent requests (DAOS-18607).
* Fix incorrect object shard ID assembly in CPD RPC handler when punching multiple shards of
  the same object on the same VOS target, which caused some shards to leak (DAOS-18641).
* Skip key checksum verification during value enumeration, fixing a crash where a dummy IOD
  passed to daos\_csummer\_verify\_iod caused ISA-L SHA-256 update to fail (DAOS-18356).

#### Placement

* Simplify the jump-map state machine for rebuild: properly set rebuilding/reintegrating flags
  for all target transitions and remove the requirement for an extra target when rebuilding
  DOWN-to-UP targets (DAOS-18487).
* Remove expensive `_get_target/_get_dom` traversals in layout computation and use byte-level
  bitmap skipping instead of bit-by-bit checking, significantly improving performance for
  large objects (DAOS-17444).
* Stop layout computation early for rebuild and EC aggregation—only; generate the layout for
  the requested redundancy group rather than the full object, saving significant CPU (DAOS-18607).

#### DTX (Distributed Transactions)

* Allow a DTX participant whose target was in rebuild/reint at transaction time to become
  the new DTX leader once its status returns to UPIN, resolving stuck transactions after
  failover (DAOS-18728).
* Add the ability to discard invalid DTX records discovered by the consistency checker (ddb),
  enabling recovery from inconsistent transaction metadata (DAOS-16951).

#### VOS (Versioned Object Store)

* Cap the merged extent size at a defined threshold to prevent runaway memory consumption
  during extent coalescing (DAOS-18901).
* Lower the anti-fragmentation system space reservation: reduce min from 2 GB to 600 MB and
  max from 10 GB to 6 GB while keeping the 5% ratio; also allow GC to use smaller credits
  when encountering ENOSPACE to reclaim space in tighter conditions (DAOS-17345, DAOS-18690).
* Update PMDK to fix a heap\_curr\_allocated accounting underflow that could produce spurious
  "pool not closed" messages and incorrect free-space statistics (DAOS-18882).
* Add missing btr\_node\_tx\_add() calls when changing btree node flags and during node splitting,
  preventing potential metadata inconsistencies; also stop ignoring umem return codes that signal
  a broken transaction, avoiding engine crashes (DAOS-18531, DAOS-17891).

#### Pool / Container

* Populate pool handles into the IV cache during PS leader step-up so that targets can access
  handles immediately after all pool services restart (DAOS-17351).
* Retry pool handle IV fetches on transient errors and ensure
  ds\_pool\_iv\_conn\_hdls\_update is called even when no handles exist in the DB,
  fixing invalid IV entries that returned unexpected DER\_NOTLEADER (DAOS-18613).
* Make the checkpoint ULT always yield when it fails to acquire DMA buffers, preventing it
  from blocking the xstream; also tune default checkpoint parameters (DAOS-18691, DAOS-18366).
* Prevent ds\_rsvc\_start from inserting a pool service into the hash table while the ds\_pool
  is stopping, fixing a hang during concurrent pool create and pool stop (DAOS-18552).
* Allow a pool to start even when some shards are missing, so pool operations like
  `dmg pool list` and `dmg pool query` remain functional during partial outages (DAOS-18036).
* Break the infinite IV retry loop in cont\_track\_eph\_leader\_ult when the ULT needs to exit,
  preventing pool service leader hangs after network outages (DAOS-18240).
* When pool self\_heal is set to "exclude" (no rebuild), still allow admin-initiated manual
  rebuilds; also support "none" as a valid self\_heal property value (DAOS-15993, DAOS-17973).
* Treat a rank as failed in cont\_agg\_eph\_sync if all its targets are excluded, so the EC
  aggregation boundary epoch is correctly propagated to other engines (DAOS-18157).

#### DFS (DAOS File System)

* Harden error handling and fix memory leaks across multiple DFS entry points including
  lookup, readdir, and object attribute operations (DAOS-18697).
* If dir-oclass is set to EC on container create, fall back to the default; `daos fs set-attr`
  with an EC class now applies only to files while directories use the default (DAOS-18604).
* Correctly display directory and file object classes in `daos fs get-attr`; also add DFS
  chunk size selection for RF3 containers with 3-parity EC (DAOS-17523, DAOS-18171).

#### CaRT (Transport)

* Remove the logic that forces tagged unexpected messages for non-CXI/TCP providers, enabling
  OFI multi-recv on InfiniBand and improving message throughput (DAOS-18484).
* Add a tunable SWIM\_SUBGROUP\_SIZE environment variable to control the number of indirect-ping
  targets; also log new SWIM suspicions at INFO level with origins for easier debugging
  (DAOS-17405).
* Remove legacy PSR (Primary Service Rank) code that was only used by CaRT tests and samples;
  improve context destroy sequencing to avoid cleanup races (DAOS-17114, DAOS-13887).
* Add resubmitted out-of-quota RPCs to the timeout tracking list so they can be properly
  timed out; fix corpc error handling that caused double-reply or refcount leaks on middle
  nodes (DAOS-17470, DAOS-17861).

#### BIO (Block I/O)

* Monitor inflight SPDK I/Os and raise a RAS event if any I/O is not completed within a
  configurable timeout (120 s default via DAOS\_SPDK\_IO\_TIMEOUT),
  marking the device as faulty (DAOS-17607).
* Enable the auto-faulty reaction with default thresholds by default; previously it required
  explicit opt-in via daos\_nvme.conf (DAOS-18337).
* Flush the WAL header (persisting the last checkpointed ID) before unmapping the
  checkpointed region, preventing stale WAL replay if the engine crashes in between
  (DAOS-17628).
* Remove the legacy si\_unused\_id rollback on WAL commit failure, which violated the invariant
  that new transaction IDs must exceed the last checkpointed ID (DAOS-18615).

#### Control Plane

* Refuse to start daos\_server when Transparent Huge Pages (THP) are enabled, as THP causes
  SPDK hugepage fragmentation and memory accounting issues (DAOS-17468).
* Scale `dmg pool` command timeouts proportionally with the number of ranks in the system to
  avoid premature timeouts on large clusters (DAOS-18366).
* Restrict Excluded ranks to only transition to Joined or AdminExcluded states, preventing
  confusing state changes when excluded engines SIGKILL themselves (DAOS-17643).
* Remove stale SPDK lockfiles both on engine exit and before/after NVMe local scans to avoid
  scan failures on restart (DAOS-17341, DAOS-17935).
* Add ComponentServer gRPC authorization for dmg system drain/reintegrate/self-heal/rebuild
  commands so server-to-server calls succeed in certificate mode (DAOS-18198).
* Fix reintegration error handling: add "failout" to MGMT\_TGT\_CREATE CoRPC to avoid leaking
  pools on ranks being reintegrated; also fix pool create cleanup logic (DAOS-17600, DAOS-18162).
* Fail protocol query after all engines have been tried instead of retrying indefinitely,
  avoiding an infinite flood of errors when engines are offline (DAOS-18167).
* Improve engine start issue handling: skip stuck bio\_xsctxt\_free loops after SPDK init
  failure, restrict nonexistent-child tolerance to CR mode, add a RAS event on pool start
  failures, and skip pools during engine start if needed (DAOS-17442, DAOS-17305).

#### Consistency Checker

* When a dRPC upcall to the control plane fails, properly remove the pending interaction record
  from the check instance tree before destroying it, preventing assertion failures; also fix
  container label boundary checking for non-null-terminated buffers (DAOS-18587).
* Handle rank death events from both SWIM and CaRT process group notifications to ensure no
  event is missed during consistency repair operations (DAOS-18238).
* Destroy check instance after check cleanup; filter out repeated pools in the check list to
  avoid redundant processing (DAOS-18441, DAOS-17822).

#### Utilities / Common

* ddb: Replace device offline, zero-length key fix, interpret key by type (DAOS-17180, DAOS-16963,
  DAOS-18625).
* Fix rare DAV VOS heap chunk metadata state during engine restart that could cause
  future allocations to abort with an assertion failure (DAOS-18195).
* Redirect PMDK internal error and warning messages to VOS logging instead of stderr,
  making them visible in the DAOS log with proper filtering (DAOS-16661).
* Run vos\_pool\_create and vos\_pool\_open in deep-stack ULTs to prevent pmemobj\_create/open
  from overflowing the caller's stack (DAOS-18296).

### Known Issues

An issue with memory registration handling in the libfabric cxi provider
may cause DER\_NOMEM errors during rebuild. This issue is fixed in
libfabric PR https://github.com/ofiwg/libfabric/pull/11908,
which has been landed in libfabric but is not included in the libfabric
version shipped with the current Slingshot Host Stack (SHS).
The workaround is to install the latest libfabric, which includes
this PR (DAOS-18326).

The default value for NA\_OFI\_UNEXPECTED\_TAG\_MSG was 1 until DAOS 2.6.4
but has been changed to 0 in DAOS 2.6.5.
In environments with different code versions on the clients and servers,
the same value needs to be set to allow clients and servers to communicate.
The preferred value for both is 0, so no action is needed on 2.6.5
but NA\_OFI\_UNEXPECTED\_TAG\_MSG=0 must be explicitly set on 2.6.4
or older (DAOS-18964).


## DAOS Version 2.6.4 (2025-10-29, updated 2025-11-04)

The DAOS 2.6.4 release includes the daos-2.6.4-7 RPM packages and their prerequisites.
It contains the following updates on top of DAOS 2.6.3:

* Libfabric has been updated to version 1.20.0.
* Mercury has been upgraded to version 2.4.0-8.
* Argobots has been upgraded to version 1.2-3.
* Libisal has been upgraded to version 2.31.1-7.
* pmemobj has been upgraded to version 2.1.0-6.

### Bug fixes and improvements

This release includes fixes and improvements, including:

* Avoid potential races between rebuild and EC aggregation by serializing these two operations;
  this prevents inconsistencies between data and parity.
* Store EC aggregation epoch in RDB so the rebuild system retains the value after system restart.
* Add two new DFUSE options: --dump-handles to dump pool, container, and DFS handles when mounting
  dfuse on a client. Other clients can use the dumped information to mount dfuse by using the
  --read-handles option. This mechanism uses the local2global and global2local to serialize and
  share handles from a root process to other processes.
* Correct how the JCH placement algorithm invokes CRC functions and improve data balancing.
* Fixes for EC object consistency verification so it can detect inconsistencies between data
  and parity.
* Merge small or fragmented buffers on the client side to reduce IOV count and mitigate resource
  exhaustion during network bulk transfers.
* Enable EC\_xP3 and use EC\_xP3 for automatic object-class selection.
* Fix a bug in the rebuild system related to waking up throttled ULTs, improving rebuild
  performance.
* Fix pool/container handle behavior after fork.
* Multiple fixes for DTX background services to avoid orphan DTX entries and inconsistent DTX
  participant states caused by intermittent network failures.
* Workaround for SPDK AMD IOMMU support.
* Fix a race condition between fetch and aggregation that could cause degraded fetch to return
  an incorrect view of data.
* Reset the DTX base UUID after fork to avoid parent and child processes generating identical
  DTX IDs.
* Fix a race between DTX refresh and abort that could leave a transaction in an unexpected state.
* Multiple fixes for EC degraded fetch of single values.
* Fix size queries for array values protected by erasure coding.
* Allow modification of a pool's redundancy factor on existing pools.
* Fix a race between DTX aggregation and container close that could cause an assertion failure.
* Multiple fixes in the rebuild system to prevent rebuild hangs on unstable networks.
* Increase the retry delay for collective RPCs to avoid generating large bursts of network traffic.
* Set the DFS chunk size on container creation (if not set by the caller) to a multiple of the full
  EC stripe length to avoid misalignment. This ensures that the default DFS chunk size does not
  create partial stripe updates if the IO size is full stripe. This auto setting works for
  containers created with rd_fac 1 or 2. EC for rd_fac 3 was newly added and missed in this
  optimization so users would have to set the chunk-size manually for that. This will be fixed next
  release.
* Remove the separate RPM build for raft.
* Add a new environment variable, D\_QUOTA\_BULKS. When enabled, it limits the number of active
  Mercury bulk allocations by postponing those that exceed the limit.
* DMG's exclude, drain, and reintegrate operations accept --ranks or --rank-hosts in ranged
  format, allowing the same operation to be applied to those ranks across all pools they belong to.
* Ensure server-side verification does not cause a target to be disabled because of checksum
  errors that are unrelated to the device.
* Avoid returning non-retryable errors to clients when an SSD is marked FAULTY, allowing
  applications to continue accessing data in degraded mode.

### Known Issues and limitations

* An RPM dependency in daos-2.6.4-6 and mercury-2.4.0-7 causes their installation
  to fail on systems with Slingshot Host Stack (SHS) version 11 and 12.
  This dependency has been corrected in daos-2.6.4-7 and mercury-2.4.0-8,
  which are otherwise identical.

* With MD-on-SSD, when creating a pool with the `--size=xx%` option, the available
  SCM capacity is used to calculate the absolute tier sizes (not the available NVMe
  capacity). When the actual ratio of SCM capacity to NVMe capacity in the system
  is smaller than the default tier-ratio of 6% SCM, this will leave some NVMe
  capacity unused. The workaround is to specify absolute per-engine sizes with the
  `--scm-size`and `--nvme-size` options of `dmg pool create`.

## DAOS Version 2.6.3 (2025-03-26)

The DAOS 2.6.3 release includes the daos-2.6.3-4 RPM packages and its prerequisites.
It contains the following updates on top of DAOS 2.6.2:

* Operating system support: Added SLES/Leap 15 SP6 support

* libfabric has been updated from 1.22.0-1 to 1.22.0-2

* PMDK (libpmem\*) has been updated from 2.1.0-2 to 2.1.0-3

* Mercury has been updated from 2.4.0-1 to 2.4.0-3

### Bug fixes and improvements

The DAOS 2.6.3 release includes fixes for several defects:

* Refresh DAOS agent URI cache after DAOS engine(s) have been excluded.
* Clear io contexts for unplugged faulty device, otherwise it can cause engine coredump.
* Fix a bug in collective punch for sparse ranks, which may cause illegal memory access and engine coredump.
* Increase stack size for more IV ULTs and avoid stack overflow.
* Populate stat::f\_fsid for intercepted statfs().
* Add credits and flow control for server-side RPC forwarding and avoid RPC congestion.
* Several fixes for DTX to avoid inconsistency between VOS data structure and persistent DTX status,
  the inconsistency can cause assertion failure on DTX resync.
* Fix a bug for tearing down faulty NVMe device, which can cause engine coredump.
* Limit the number of extents being passed to DAOS to 16k, if the extent sizes are under 16 bytes.
* Always use SIGKILL to shutdown DAOS engine, instead of running through finalizing processes of modules.
  The latter may cause false eviction and trigger unnecessary rebuild. This is a temporary solution.
* Include EP flush patch to Mercury.
* Cache pool map bulk descriptor on service leader instead of recreating it for each query.
  Otherwise service leader may run out of low-level network stack resources while handling intensive pool query.
* Fix a bug in aggregation which may cause integer overflow and assertion failure on the server.
* Add 3-fault-tolerant EC object classes (like EC\_16P3).
* Add a few new functionalities to the DDB tool.
* Several fixes for usability improvements for the control plane.
* Automatically retrigger rebuild when number of unreachable engines decreased to less than or equal to RF of pool.
* Fix a double free of collective RPC.

For details, please refer to the Github
[release/2.6 commit history](https://github.com/daos-stack/daos/commits/release/2.6)
and the associated [Jira tickets](https://jira.daos.io/) as stated in the commit messages.

## DAOS Version 2.6.2 (2024-12-10)

The DAOS 2.6.2 release includes the daos-2.6.2-2 RPM packages and its prerequisites.
It contains the following updates on top of DAOS 2.6.1:

* Bump hadoop-common from 3.3.6 to 3.4.0

### Bug fixes and improvements

The DAOS 2.6.2 release includes fixes for several defects:

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

The DAOS 2.6.1 release includes the daos-2.6.1-3 RPM packages and its prerequisites.
It contains the following updates on top of DAOS 2.6.0:

* Mercury update for slingshot 11.0 host stack and other UCX provider fixes.

### Bug fixes and improvements

The DAOS 2.6.1 release includes fixes for several defects and a few changes
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

The DAOS 2.6.0 release includes the daos-2.6.0-3 RPM packages and its prerequisites.
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

The DAOS 2.6.0 release includes fixes for numerous defects.
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
