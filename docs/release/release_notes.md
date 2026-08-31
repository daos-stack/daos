# DAOS Version 2.8 Release Notes

## DAOS Version 2.8.0 (2026-08-12)

We are pleased to announce the release of DAOS version 2.8.

### General Support

The DAOS 2.8.0 release includes the
[daos-2.8.0 RPM packages](https://packages.daos.io/v2.8.0/) and its
prerequisites. DAOS Version 2.8.0 supports the following environments:

Architecture Support:

* DAOS 2.8.0 supports the x86\_64 architecture.

Operating System Support:

* SLES 15 SP6 and SP7
* openSUSE Leap 15.6
* EL 9.6/9.7 (RHEL, Rocky Linux, Alma Linux)

Fabric and Network Provider Support:

* libfabric support for the following fabrics and providers:

    - ofi+tcp on all networking hardware
    - ofi+tcp;ofi\_rxm when RXM is explicitly requested
    - ofi+verbs on fabrics that support the verbs API
    - ofi+cxi on HPE Slingshot fabrics

* [UCX](https://docs.daos.io/v2.8/admin/ucx/) support on NVIDIA InfiniBand and
  RoCE fabrics:

    - ucx+dc\_x is the recommended UCX provider

Storage Class Memory Support (PMem and non-PMem servers):

* DAOS Servers without Intel Optane Persistent Memory, using the production
  Metadata-on-SSD Phase2 code path with allocator v2 as the default for newly
  created pools.
* Existing Metadata-on-SSD Phase1 pools remain supported and retain their
  original allocator when opened.
* DAOS Servers with 3rd gen Intel Xeon Scalable processors and Intel Optane
  Persistent Memory 200 Series.
* DAOS Servers with 2nd gen Intel Xeon Scalable processors and Intel Optane
  Persistent Memory 100 Series.

For a complete list of supported hardware and software, refer to the [Support
Matrix](https://docs.daos.io/v2.8/release/support_matrix/)

### Key features and improvements

#### Software Version Currency

* See [above](#General-Support) for supported operating system levels.

* Refer to the [Support
  Matrix](https://docs.daos.io/v2.8/release/support_matrix/) for supported
  fabric providers and vendor software combinations.

* The following prerequisite software packages used by the DAOS build have been
  updated:

    - Libfabric has been updated to 1.22.0
    - UCX (in DOCA-OFED) has been updated to 1.20.0
    - Mercury has been updated to 2.4.1
    - SPDK has been updated to 26.01
    - PMDK has been updated to 2.1.3
    - ISA-L has been updated to 2.31.1
    - ISA-L Crypto has been updated to 2.26
    - Argobots has been updated to 1.2
    - Protobuf-C remains at 1.3.3
    - Fused remains at 1.0.0

* The Go module and RPM build metadata require Go 1.21.

#### New Features and Usability Improvements

##### MD-on-SSD Phase 2

* It is a primary feature of DAOS Version 2.8 that the Metadata-on-SSD
  bucket-memory allocator v2 is now production ready. This allocator is now
  the default metadata allocator for newly created Metadata-on-SSD pools.
  It allows all new Metadata-on-SSD deployments to use the v2 allocator
  and establishes a common allocator path, so the allocator v1 can be retired
  in the longer term instead of maintaining two allocator implementations
  indefinitely.

* The production ready default in DAOS Version 2.8 is to create MD-on-SSD
  pools with a 100% memory-to-metadata ratio (`--mem-ratio 100%`),
  so metadata eviction is not enabled.
  As a technoloy preview, `dmg pool create` supports metadata sizing
  with a memory-to-metadata ratio smaller than 100% (for example, using
  `--mem-ratio 25%`).

* Existing MD-on-SSD Phase1 pools remain on their original allocator and are
  not converted when opened. To migrate Phase1 pools, data in those pools
  needs to be copied by the user to a new pool created with the v2 allocator.

* Allocator v2 supplies the durable format, bucket-aware allocation, cache,
  pinning, object preload, garbage collection, DTX, CPD, and
  accounting groundwork needed for future metadata eviction.

* Allocator v2 also provides the foundation for future metadata eviction, which
  is intended to support metadata capacity larger than the in-memory metadata
  cache. Metadata eviction itself is not production ready and has not been
  validated by the community; this limitation does not apply to Phase2 operation
  without metadata eviction.

* Pool create, extend, reintegration, list, query, storage display, DDB,
  recovery, and pool recreation paths preserve the Metadata-on-SSD sizing
  and format state required by Phase2.

* The VOS-file size is recorded independently from the metadata-blob size where
  they differ. This improves recovery and recreation handling for configurations
  in which the cache file and durable metadata allocation are not the same size.

* Phase2 improves small-metadata allocation, empty-slab reclamation,
  memory-bucket accounting, address conversion, and aggregation tuning under
  metadata-space pressure. Low `--mem-ratio` configurations also receive
  corrected sizing and accounting behavior.

##### Check and Repair (CR) Utilities and Framework

* The `dmg check` command is the workflow for checking and repairing
  DAOS system metadata. DAOS 2.8 expands and hardens the CR Technology
  Preview framework rather than presenting it as an general-purpose data repair
  facility.

* DAOS 2.8 adds Metadata-on-SSD mode support to CR utilities, allowing check and
  repair workflows to operate on systems using that storage layout.

* The `dmg check --for-all` option has been removed. Administrators can use
  `dmg check set-policy` to apply a repair decision to interactions of the same
  class, making repeated repair behavior explicit and reviewable.

* Repeated pool arguments are filtered. Orphan processing preserves shards that
  are still needed by incremental reintegration rather than treating all such
  shards as immediately disposable.

* `dmg check query` reports unchecked pools without requiring verbose output,
  identifies dry-run results as not applied, returns reports in stable order,
  and manages stale findings from previous checker runs.

* A checker dry run keeps checked pools in immutable maintenance mode. Rebuild,
  aggregation, data movement, and write access remain blocked until the
  maintenance condition is resolved.

* Checker reporting, per-engine repair, parallel inconsistency processing,
  engine shutdown, rank-death, and cleanup paths have been hardened. These
  changes improve administrator feedback and reduce stranded checker state when
  an inspection or repair is interrupted.

##### Rebuild Performance and Stability

* Rebuild migration now processes object IDs on main xstreams and uses a
  per-target global migration-resource manager. ULT and DMA-buffer use is
  limited across all pools on a target instead of being independently
  overcommitted by each rebuilding pool.

* Compatible multi-rank rebuild operations can be combined. EC scanning,
  reclaim, aggregation, and partial-layout generation use improved pacing
  and fewer resources, reducing contention with foreground I/O and other
  recovery work.

* Rebuild resilience is improved for recoverable network failures, pool service
  leader retry, interruption, massive failures, and migration destinations that
  depart while work is in progress.

* EC rebuild and aggregation fixes cover partial-parity epochs, checksum
  handling, partial overwrites, parity-target consistency, epoch-boundary
  persistence, restart, discard completion, and degraded-fetch races.

* Rebuild status and return-code fixes improve detection of completed global
  scans, and ensure migration setup failures are returned to the controlling
  service rather than being reported as success.

##### Interactive Rebuild Control

* DAOS Version 2.8 adds controls for stopping and restarting rebuild for an
  individual pool, to handle exceptional rebuild situations:

    - `dmg pool <pool> rebuild stop [--force]`
    - `dmg pool <pool> rebuild start`

* These are not regular rebuild-administration commands. Use them only when
  rebuild has gone wrong or an administrator must halt rebuild traffic to
  diagnose and correct server issues before continuing recovery.

* A `rebuild stop` halts rebuild traffic and discards current progress.
  After fixing the server issue, `rebuild start` resumes recovery by launching
  a new rebuild. This is not a checkpointed pause or persistent disable.

* `dmg pool query --health-only <pool>` reports a stopped rebuild as idle with
  `-DER_OP_CANCELED`, allowing automation to distinguish an administrator stop
  from successful completion.

* Interactive rebuild stop is phase-aware. An ordinary stop works during rebuild,
  is rejected during successful reclaim, and allows fail-reclaim cleanup
  to finish while suppressing the next retry.

* `--force` is reserved for a fail-reclaim retry loop after fail-reclaim has
  failed at least once. Follow-on fixes prevent rejected stops from latching
  state and preserve cleanup broadcasts required by stopped reintegration.

##### System-Level Rebuild Control

* System-scoped commands submit the interactive request to each applicable pool:

    - `dmg system rebuild stop [--force]`
    - `dmg system rebuild start`

* These commands do not establish a separate global rebuild pause state. They
  are the system-wide administrative form of the per-pool interactive rebuild
  stop/start operation and remain distinct from automatic self-healing policy.

* Automatic recovery policy is independently configurable at system and pool
  scope through the `self_heal` property. System policy accepts `none` or an
  ordered subset of `exclude`, `pool_exclude`, and `pool_rebuild`.

* Pool policy uses `exclude`, `rebuild`, and `delay_rebuild`. Administrators can
  inspect and change policy with `dmg system set-prop` / `get-prop` and `dmg
  pool set-prop` / `get-prop`.

* `dmg system self-heal eval` re-evaluates the current system policy. System and
  pool query output identifies whether automatic exclusion or rebuild is
  disabled by system policy, pool policy, or both.

* Pool query output also reports data redundancy as normal or degraded.
  Administrative exclude, drain, reintegrate, extend, and rebuild-start
  operations can still schedule recovery independently of automatic policy.

##### Incremental Reintegration Technology Preview

* Incremental reintegration is available as an opt-in Technology Preview through
  the `reintegration:incremental` pool property. It is disabled by default and
  is not production ready.

* Incremental reintegration is not functionally complete. Punches occurring
  after rebuild and before reintegration are not fully handled and may not be
  reproduced correctly on the reintegrated target.

* The implementation maintains a global stable epoch and reintegrates from that
  point instead of rebuilding the complete prior history. This reduces the
  intended reintegration scope but does not yet provide all semantics of the
  default reintegration mode.

* Production deployments should continue to use the default `data_sync` mode.
  The technology preview is intended for evaluation of the new mechanism and its
  administrative workflow.

##### Control Plane and Administration

* Pool exclude, drain, and reintegrate accept multiple ranks. System-level drain
  and reintegrate apply the requested operation to affected pools throughout the
  system.

* Notable changes in the `daos_server.yml` configuration file:

    - `access_points` is now deprecated; use `mgmt_svc_replicas` instead.
    - A `control_iface` option is added to set a specific network interface for
      the control plane listener (by default, it will listen on all interfaces)
    - Configurations generated by `dmg config generate` now enable NVMe hotplug
      by default.
    - SPDK I/O-buffer tuning options are exposed to allow customization for
      large NVMe disks: A new `spdk_iobuf` section allows to explicitly set
      `small_pool_count` and `large_pool_count`.

* The SPDK configuration is validated at engine start, and
  SPDK configuration updates that are not allowed require an explicit override.
  This makes potentially disruptive configuration changes visible to
  administrators rather than applying them implicitly.

* Storage preparation and startup handling improve hugepage allocation and
  permissions, SPDK lock-file cleanup, preparation with one missing SCM
  namespace, and reporting for Metadata-on-SSD, non-PMem, and emulated NVMe
  configurations.

* Server startup rejects conflicting transparent-hugepage settings.

* Packaging creates persistent writable server locations, protects existing
  configuration files during installation.

* Default daemon logfile location has changed from `/tmp` to `/var/log/daos`.

* The control plane can automatically restart a configured excluded rank
  following an engine suicide event, avoids starting administratively excluded
  ranks, and supports graceful shutdown of selected ranks.

* NVMe health output reports PCIe link speed and width and can emit a RAS event
  for a downgraded link. Storage fault and replacement commands require an
  unambiguous host, and blacklisted VMD devices are not unbound.

* Container listing includes unlabeled containers and displays their UUIDs in
  non-verbose output, so administrators can identify containers that do not have
  user-assigned labels.

##### DFS and dfuse

* As a Technology Preview, DFS and dfuse can mount a container snapshot
  read-only by snapshot name or epoch. This feature is not production ready.

* Client-side DFS telemetry adds operation counters and read/write byte
  histograms for POSIX containers, providing workload visibility at the DFS
  interface.

* DFS automatic layout selection can cycle object IDs non-sequentially and
  chooses chunk sizes aligned with complete EC stripes. Selection includes a
  practical minimum chunk size and RF3 handling.

* A `daos fs chmod` command has been added.

* Other DFS changes are fixes and hardening. They improve object-class
  reporting, update open directory handles after class changes, prevent EC
  classes from being assigned to directories, and reject unsafe force removal
  from cached system mounts. DFS error handling and memory ownership are hardened

* dfuse reads in larger chunks, associates pre-read state with inodes, and
  improves directory caching, readdir, and shutdown behavior.

##### Observability and Telemetry

* Prometheus export supports histograms, and telemetry registration can be
  selective. Administrators can limit registration to the metrics needed by
  their monitoring environment.

* Per-NUMA memory information and SPDK I/O monitoring are available. VOS, DFS,
  networking, and storage metrics receive reporting and lifecycle corrections.

* RPC origin addresses, Mercury counters, CaRT counter dumps, and standardized
  timestamps improve correlation of client, transport, and server activity.

* Log rotation retains the first `.old` file for diagnostics.

* `dmg support collect-log` no longer requires optional rsync aggregation,
  simplifying support collection on installations without that component.

##### Reliability, Recovery, and Data Integrity

* Object and VOS changes add stricter array-IOD extent validation, improved
  client restart and conditional-operation retry, congestion pacing, and safer
  sparse-layout and resent collective-RPC handling.

* An N+3 EC object class is available for automatic class selection.

* EC fetch, recovery, enumeration, aggregation, consistency verification,
  and rotated enumeration receive correctness fixes.

* VOS can mark corruption at object, dkey, and akey granularity. Scrub and
  checker iteration better respect visibility, operation intent, and concurrent
  container destruction.

* DTX fixes address partial commit, orphan entries, refresh and resync,
  aggregation, eviction, participant and leader races, duplicate batched
  commits, closed containers, and rebuild visibility of uncommitted entries.

* DTX collective-RPC load can be limited. Large merged extents are capped, and
  huge single values can use gang allocation. These changes reduce pathological
  memory and transaction pressure.

* SPDK hotplug and automatic faulty-device handling are hardened and enabled by
  default in generated configurations. Engine handling avoids aborting when an
  NVMe device is removed.

* WAL flushing, transaction sizing, and BIO asynchronous-I/O defaults are
  improved. Pool and container protocol handling, service startup and shutdown,
  handle persistence, and RDB lease behavior receive corrections.

* Raft node-ID and upgrade-validation fixes improve service recovery.
  Unsupported pool-layout upgrade jumps are rejected instead of risking an
  incompatible transition.

##### Client APIs, Tools, and Compatibility

* `libpil4dfs` adds or corrects interception for `creat`, `creat64`, `renameat`,
  `fchown`, `fgetxattr`, `fsetxattr`, `fdatasync`, `FIOCLEX`, `dup3`, and
  `utimensat`.

* `libpil4dfs` also adds whitelist mode and dentry-cache garbage collection.
  Safety is improved around `exec`, `fork`, MPI, dynamic loading, and
  accelerator-runtime initialization.

* `pydaos.torch` adds checkpoint integration, automatic checkpoint-path
  creation, batch reads, and dataset directory-object caching.

* PyDAOS dictionary cleanup has an explicit destroy operation.

* CaRT accepts free-form UCX provider specifications, improves multi-interface
  fabric-domain parsing, supports explicit client and agent interfaces, and
  retries transient UCX or CXI initialization failures.

* Mercury includes UCX and key-resolution fixes. OFI multi-receive is enabled
  where supported. Mercury bulk/RPC timeout and quota behavior are refined.

* Client package installation refreshes the dynamic-linker cache, preventing
  later package operations from leaving libdaos consumers with stale linker
  state.

#### Better pool and container property defaults

* The default redundancy factor that is set on the pool level is now set to
  `rd_fac=3`; the previous default was zero. Container create operations will
  inherit this pool-level default unless a different redundancy factor is
  explicitly specified at container creation time. This means that new
  containers created with DAOS Version 2.8 will now have 3-fault-tolerance
  by default. Note that if the number of fault domains in the pool
  (servers that are participating in the pool) is smaller than three,
  the `rd_fac` will be adjusted down accordingly.

* The default fraction of pool space that is reserved for rebuild (`space_rb`)
  is now 5%. In previous releases, no space was reserved by default.

* The default `ec_cell_sz` has been increased from 64kiB to 128kiB.
  This improves EC performance.

#### Other notable changes

* Generated server configurations enable the supported NVMe hotplug path by
  default and expose storage tuning without changing the requirement to validate
  hardware and provider combinations against the Support Matrix.

* The dfuse single-threaded option has been removed. Deployments that still pass
  the deprecated option must update their launch configuration.

* Build, packaging, test, and functional-test maintenance updates supported
  operating-system images, dependency tooling, fault injection, and release
  qualification. Such maintenance is not presented as new product functionality.

### Known Issues and Limitations

* Metadata-on-SSD Phase2 with allocator v2 is production ready when metadata
  eviction is not enabled. Metadata eviction itself is not production ready and
  has not been validated by the community. It remains future work intended to
  allow metadata capacity to exceed the in-memory metadata cache.

* Existing Phase1 pools are not converted to allocator v2 when opened. Allocator
  v2 is the default for newly created Metadata-on-SSD pools, but this release
  does not provide automatic migration of existing pools.

* Incremental reintegration is an opt-in Technology Preview, is not functionally
  complete, and is not production ready. Punches occurring between rebuild and
  reintegration are not fully supported. Use the default `data_sync` mode for
  production reintegration.

* Interactive rebuild stop/start is an exceptional recovery workflow for halting
  rebuild traffic while server issues are corrected; it is not a regular or
  persistent pause/resume facility. A stop discards current progress, and start
  launches a new rebuild. System-level stop/start sends those operations to
  pools and is distinct from disabling automatic self-healing.

* Check and Repair remains a recovery-oriented preview workflow with maintenance
  restrictions. A dry run leaves checked pools in immutable maintenance mode
  until the condition is resolved.

* When downgrading from DAOS version 2.8 back to Version 2.6, the following
  changes to the SPDK configuration file `daos_nvme.conf` have to be manually
  applied on all servers, to revert a parameter name change that was introduced
  by the newer SPDK version in DAOS version 2.8:

```bash
  # For PMem based servers, with 2 engines per server:
  export PATH0=/mnt/daos0 # matching the scm_mount path for engine 0 in daos_server.yml
  export PATH1=/mnt/daos1 # matching the scm_mount path for engine 1 in daos_server.yml

  # For MD-on-SSD based servers, with 2 engines per server:
  export CONTROL_METADATA=/mnt # matching the control_metadata path in daos_server.yml
  export PATH0=$CONTROL_METADATA/daos_control/engine0
  export PATH1=$CONTROL_METADATA/daos_control/engine1

  sed -i 's/vmd_enable/enable_vmd/g' $PATH0/daos_nvme.conf
  sed -i 's/vmd_enable/enable_vmd/g' $PATH1/daos_nvme.conf
  sed -i 's/transport_retry_count/retry_count/g' $PATH0/daos_nvme.conf
  sed -i 's/transport_retry_count/retry_count/g' $PATH1/daos_nvme.conf
```

### Bug fixes

The DAOS 2.8.0 release includes fixes for numerous defects in rebuild, object,
VOS, DTX, erasure coding, pool and container services, DFS, dfuse, `libpil4dfs`,
storage, networking, telemetry, and administrative utilities. For details,
please refer to the Github [release/2.8 commit
history](https://github.com/daos-stack/daos/commits/release/2.8) and the
associated [Jira tickets](https://daosio.atlassian.net/jira) as stated in the
commit messages.

## Additional resources

Visit the [online documentation](https://docs.daos.io/v2.8/) for more
information. All DAOS project source code is maintained in the
[https://github.com/daos-stack/daos](https://github.com/daos-stack/daos)
repository. Please visit this
[link](https://github.com/daos-stack/daos/blob/release/2.8/LICENSE) for more
information on the DAOS license.

Refer to the [System Deployment](https://docs.daos.io/v2.8/admin/deployment/)
section of the [DAOS Administration
Guide](https://docs.daos.io/v2.8/admin/hardware/) for installation details.
