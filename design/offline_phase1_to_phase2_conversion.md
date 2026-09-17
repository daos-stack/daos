# Summary

-   **Problem**: A legacy MD-on-SSD pool created with a 100% memory ratio operates in phase1 mode. All existing metadata is non-evictable (NE), and the META blob has no evictable capacity for phase2 allocations.

-   **Proposal**: Add a `dmg pool resize` operation that makes the pool temporarily unavailable, enlarges every target's META blob, slightly enlarges every VOS memory file, and enables phase2. Existing DAV2 zones remain NE; newly appended META zones provide evictable (E) capacity.

-   **Impact**: Conversion preserves existing offsets and requires no metadata relocation or object-tree scan. The pool gains metadata capacity while its memory footprint grows only by a bounded NE reserve. Pool connections and I/O are unavailable for the duration of the operation.

# Background

## Current Architecture

MD-on-SSD stores durable VOS metadata in an SSD META blob and uses a VOS file on tmpfs as the memory cache. The ratio between memory-file size and META-blob size determines the operating mode:

-   **Phase1 (100% mem-ratio):** The memory file is the same size as the META blob. The complete heap is resident and all allocated DAV2 zones are NE.

-   **Phase2 (< 100% mem-ratio):** The META blob is larger than the memory file. Shared metadata stays resident in NE zones, while object-private metadata can be allocated from E zones and loaded or evicted on demand.

This design assumes that the phase1 pool already uses the DAV2 backend (`DAOS_MD_BMEM_V2`). DAV1-to-DAV2 migration is out of scope. DAV2 persists the conversion-relevant geometry and mode in several places:

-   `heap_header.heap_size` records the META capacity visible to DAV2.

-   `heap_header.cache_size` records the VOS memory-file size.

-   `heap_header.nemb_pct` limits the NE share of the memory cache.

-   The DAV2 zone information vector records whether a zone is allocated and evictable.

-   The BIO metadata header records META capacity, backend type, and `META_HDR_FL_EVICTABLE`.

-   SMD records the META blob size and target-to-blob mapping.

-   `vos_pool_df.pd_scm_sz` records VOS metadata capacity, and `vos_pool_ext_df.ped_mem_sz` records the memory-file size.

DAV2-backed phase1 pools already use `struct vos_obj_p2_df`, because object format selection depends on the DAV2 backend type rather than the evictable flag. Existing phase1 objects have no E bucket and continue to use `UMEM_DEFAULT_MBKT_ID` after conversion.

## Expansion Model

The conversion does not shrink the VOS memory file. A phase1 pool may have filled almost all of its current META blob with NE allocations. Starting phase2 with an equal or smaller cache would leave no memory headroom for GC, DTX, allocator metadata, SOE buckets, or other NE growth.

Instead, conversion uses this geometry:

-   $B_{old}$ is the old META-blob size and old memory-file size.

-   $H_{NE}$ is additional memory reserved for future NE and SOE use.

-   $C_E$ is the minimum resident cache working set for E buckets.

-   $M_{new} = B_{old} + H_{NE} + C_E$ is the enlarged memory-file size.

-   $R$ is the requested phase2 memory ratio, where $0 < R < 1$.

-   $B_{new}$ is the enlarged META-blob size:

$$
B_{new} = \operatorname{align}\left(\frac{M_{new}}{R}\right)
$$

The old DAV2 zones remain NE and preserve all existing offsets. $H_{NE}$ tolerates a full legacy heap and subsequent NE growth, while $C_E$ ensures that phase2 can keep a useful number of E buckets resident. Zones introduced by extending the META blob start unused and are available for DAV2 to allot as E buckets. No existing object metadata needs to move or acquire an E-bucket ID.

# Goals and Non-Goals

## Goals

-   **Pool-Level Administration**: Expose conversion through `dmg pool`, coordinated by the management service and pool service.

-   **Offline Pool Semantics**: Reject new connections, evict existing connections, drain in-flight I/O, and stop pool target activity while leaving the DAOS system and unrelated pools online.

-   **Capacity Expansion**: Enlarge each META blob enough to satisfy the requested phase2 memory ratio.

-   **NE Headroom**: Slightly enlarge each VOS memory file so a phase1 pool already full of NE data can operate safely after conversion.

-   **Stable Offsets**: Preserve existing DAV2 zone IDs, allocations, and `umem_off_t` values.

-   **Crash Recoverability**: Make conversion resumable and prevent a partially converted pool from accepting connections.

-   **Consistent Shards**: Require every target shard to commit the same conversion generation and geometry before the pool returns to service.

## Non-Goals

-   **DAV1 Migration**: Pools using the DAV1 heap layout are rejected.

-   **Online I/O**: Client I/O, aggregation, GC, scrubber, rebuild, and DTX resync do not run during conversion.

-   **Existing Metadata Reclassification**: Existing zones remain NE even if some contain only object-private allocations.

-   **Metadata Relocation**: No existing allocation or VOS object is moved.

-   **Data-Blob Expansion**: The DATA blob is unchanged.

-   **Downgrade**: Phase2-to-phase1 conversion is not supported.

# Design

## External Command

Add pool resize subcommands:

```
dmg pool resize <pool> --mem-ratio <percent> [--ne-reserve <size>] [--dry-run] [--wait[=<timeout>]]
dmg pool resize-status <pool>
dmg pool resize-cancel <pool>
```

`resize` is intentionally broader than `convert`. `dmg pool extend` remains the horizontal operation that adds ranks or targets to a pool. `dmg pool resize` is the vertical operation that changes storage geometry on the pool's existing targets. This design implements its first resize mode: expanding META and memory capacity while converting a 100% mem-ratio pool to phase2. Future vertical expansion can add size options to the same command without introducing a second overlapping API.

-   `--mem-ratio` is required and must be greater than 0% and less than 100%.

-   `--ne-reserve` optionally overrides the automatically calculated per-target NE headroom.

-   `--dry-run` validates eligibility and reports the proposed per-target geometry without changing pool availability or storage.

-   By default, `convert` submits the operation and returns as soon as the durable conversion record and background worker are established. The response contains the initial state and an informational generation UUID; it does not wait for conversion to finish.

-   `--wait` is client-side convenience. `dmg` polls `resize-status` with short RPCs until the operation reaches a terminal state. `--wait=<timeout>` limits only how long `dmg` polls; expiration does not cancel the server-side operation.

-   `resize-cancel` is accepted only while the operation is still reversible, before any META blob has been expanded. After state `RESIZING`, the operation can only resume forward.

-   `resize-status` and `resize-cancel` identify the operation by pool only. The pool service permits at most one active resize per pool, so a user-supplied generation is unnecessary. Status returns the active resize, or the most recent terminal result when no resize is active.

The completed status reports old and new META sizes, old and new memory-file sizes, effective ratio, added E capacity, and conversion generation. JSON output exposes per-rank and per-target progress.

The command follows the existing `dmg pool` request path:

1.  `dmg` sends `PoolResizeReq` to the management service.

2.  The management service resolves the pool identifier and serializes conversion with other pool administration operations.

3.  The pool service creates and commits the durable conversion record, starts a background conversion ULT, and immediately returns the generation UUID.

4.  The background ULT fences connections and advances the durable state machine. Each engine performs storage resize and DAV2/VOS activation for its local target shards.

No management, pool-service, collective, or engine RPC remains outstanding for the duration of conversion. Submission, stage dispatch, and status RPCs have normal bounded RPC lifetimes.

## Asynchronous Execution

The pool-service leader owns a background resize ULT keyed internally by pool UUID and generation UUID. RDB state, rather than ULT lifetime, is authoritative. If leadership changes or the process restarts, the new leader detects a non-terminal resize record and schedules a replacement ULT at the last durable stage.

The generation UUID remains mandatory on internal pool-service and target RPCs. It provides idempotency, rejects delayed RPCs from an earlier attempt, and correlates RDB and SMD recovery records. The management status and cancel handlers resolve the current generation under the pool-service lock before acting, so clients do not supply it.

Each potentially long target operation follows a start/query protocol:

1.  The pool-service worker sends a bounded stage-start collective containing the generation, stage, and target geometry.

2.  Each engine validates and durably records the request in SMD, schedules a local worker, and acknowledges acceptance without waiting for the storage operation to complete.

3.  The pool-service worker periodically issues bounded stage-status collectives and persists aggregate progress in RDB.

4.  A target retry with the same generation and stage is idempotent. A conflicting generation is rejected.

This prevents META resizing, memory-file growth, SPDK synchronization, or target reopen time from consuming an RPC timeout. The status includes the current stage, elapsed time, completed and total targets, per-target error, and whether cancellation is still allowed.

## Preconditions

Before taking the pool offline, the pool service verifies:

-   The pool is MD-on-SSD, uses `DAOS_MD_BMEM_V2`, and is not already evictable.

-   Current memory-file size and META-blob size describe a 100% mem-ratio pool after alignment.

-   All pool ranks and targets are available. Conversion does not run on an excluded, down, rebuilding, or reintegrating target.

-   No rebuild, upgrade, check, extend, drain, exclude, reintegrate, or destroy operation is active.

-   Every META device has enough free clusters to extend all local target blobs to the proposed size.

-   The tmpfs mount and configured memory budget can accommodate the enlarged VOS files for all pools on the engine.

-   The requested geometry is supported by DAV2's maximum zone count and zone-information-vector capacity.

Dry-run performs these checks but does not fence the pool or allocate capacity.

## Geometry and NE Reserve

The conversion computes geometry per target because alignment and current shard sizes may differ.

The default NE reserve $H_{NE}$ is the maximum of:

-   A fixed minimum number of DAV2 zones for allocator and transaction progress.

-   Space for the VOS emergency transaction buffer when not already present.

-   The configured SOE bucket reserve.

-   A percentage of the old memory-file size to absorb near-full phase1 heaps and immediate post-conversion GC/DTX growth.

The exact constants are implementation tunables and are printed by dry-run. The reserve is rounded up to DAV2 zone and memory-file allocation boundaries.

The E working set $C_E$ is computed independently as a minimum number or percentage of cache zones. It must be large enough for normal object pinning and eviction progress. `--ne-reserve` changes $H_{NE}$ only and cannot reduce $C_E$ below the implementation minimum.

For each target:

$$
M_{new} = \operatorname{align}(M_{old} + H_{NE} + C_E)
$$

$$
B_{new} = \operatorname{align}\left(\max\left(B_{old} + Z, \frac{M_{new}}{R}\right)\right)
$$

where $Z$ is one DAV2 zone. Requiring at least one added zone guarantees that the converted pool has E capacity even when rounding would otherwise produce no growth.

The effective ratio reported to the administrator is $M_{new}/B_{new}$. It must not exceed the requested ratio after rounding. Dry-run reports $H_{NE}$ and $C_E$ separately.

## Taking the Pool Offline

Offline applies to the pool, not to the entire DAOS system. Engines remain running so `dmg` can coordinate storage changes.

The pool service performs these steps:

1.  Write a durable pool conversion record with state `PREPARING` and a new generation UUID.

2.  Mark the pool non-connectable. New pool connects fail with `-DER_BUSY` and identify conversion as the reason.

3.  Evict all pool connections using the existing pool-evict mechanism.

4.  Wait until open pool and container handles are gone and all target I/O has drained.

5.  Broadcast a target quiesce RPC. Each target stops container children, aggregation, GC, scrubber, checkpoint, rebuild-related work, and DTX resync, performs a final checkpoint, then closes its VOS pool and BIO metadata context.

6.  Confirm that every target has reached `QUIESCED` before any blob is resized.

If quiescing fails, targets already quiesced are restarted with the old geometry, the conversion record is marked failed, and the pool is made connectable again.

## Durable Conversion Record

The pool service stores the authoritative distributed state in RDB:

```
struct pool_md_conversion {
    uuid_t   generation;
    uint32_t state;
    uint32_t target_count;
    uint64_t requested_ratio;
    uint64_t ne_reserve;
    uint64_t e_cache_size;
    uint64_t old_meta_size;
    uint64_t new_meta_size;
    uint64_t old_mem_size;
    uint64_t new_mem_size;
};
```

States are `SUBMITTED`, `PREPARING`, `QUIESCED`, `RESIZING`, `ACTIVATING`, `RESTARTING`, `COMMITTED`, `FAILED`, and `CANCELED`. Per-target progress records make resize and activation idempotent. A terminal `FAILED` state records whether the operation is retryable and the stage from which `dmg pool resize` with the same generation resumes.

SMD stores a local generation and state for each shard before its META blob changes. Engine startup and pool-child startup refuse normal service when SMD reports an incomplete conversion. The pool service resumes the operation forward after leadership or engine restart. Progress updates are rate-limited and batched so polling does not create excessive RDB or SMD writes.

## META Blob Expansion

BIO adds an idempotent META resize operation. For each target shard it:

1.  Opens the META blob exclusively and reads its current cluster count.

2.  If it is smaller than $B_{new}$, calls the SPDK blob resize operation and synchronizes blob metadata.

3.  Updates `meta_header.mh_tot_blks` and its checksum.

4.  Updates the SMD `sp_blob_sz` record only after SPDK reports the new durable size.

5.  Reopens and verifies the blob capacity before reporting success.

Blob expansion is monotonic. A retry observes either the old size and repeats the resize, or the new size and continues. Conversion never attempts to shrink a META blob during rollback.

The control plane reserves capacity for every participating blob before resizing the first one. This reduces the chance of a partially expanded pool due to device ENOSPC. If a later target still fails, the pool remains fenced and conversion resumes after the storage problem is corrected.

## VOS Memory-File Expansion

After META expansion, each engine enlarges the existing VOS file from $M_{old}$ to $M_{new}$ with `fallocate`. The file is never truncated by conversion.

Growing the existing file preserves its contents and all old offsets. Newly added pages are cleared before use. The operation verifies available tmpfs capacity before mutation and is idempotent when the file already has the requested size.

Unlike `ddb prov_mem`, this step runs inside the coordinated engine RPC while the pool shard is quiesced. It does not recreate the VOS file or require an engine restart.

## DAV2 Heap Extension

DAV2 gains an offline heap-extension API that accepts the new META and cache sizes. It performs these updates while the pool remains non-evictable:

-   Validate the existing heap header and old sizes.

-   Increase `heap_header.heap_size` to $B_{new}$.

-   Increase `heap_header.cache_size` to $M_{new}$.

-   Extend the zone information vector's in-use range for all newly addressable zones.

-   Clear metadata pages for the appended zones without modifying any old zone.

-   Keep all previously allocated zones marked allocated and NE.

-   Leave appended zones unallocated so phase2 can allot them as E buckets after activation.

-   Set `heap_header.nemb_pct` so the NE limit covers all old NE zones plus $H_{NE}$ and the remaining $C_E$ portion of the memory file remains available to cache E zones.

The heap header checksum is updated after all extension metadata is durable. Since old zone boundaries and offsets do not change, no VOS allocation graph rewrite is required.

## VOS and Phase2 Activation

Existing objects require no conversion. Their `vos_obj_p2_df` fields continue to identify the default NE bucket. When an existing or new object first needs phase2 object-private space, the normal `umem_allot_mb_evictable()` path allocates a bucket from an appended free zone and records it in the object.

After heap extension succeeds, each target atomically prepares these mode fields:

-   Set `vos_pool_df.pd_scm_sz` to $B_{new}$.

-   Set `vos_pool_ext_df.ped_mem_sz` to $M_{new}$.

-   Keep the BIO backend type as `DAOS_MD_BMEM_V2`.

-   Set `META_HDR_FL_EVICTABLE` in the BIO metadata header last and update its checksum.

The BIO evictable flag is the activation point because it controls `store_evictable` and `vos_pool_is_evictable()`. Once set, the shard must only be opened with the new geometry.

## Restart and Commit

Each target reopens its BIO metadata context and VOS pool using the enlarged VOS file. The phase2 open must load every old NE zone and leave appended zones unloaded until allotted.

The target then validates the heap and performs a small allocation/free cycle in a newly allotted E bucket. If all targets report ready, the pool service:

1.  Marks the distributed conversion record `COMMITTED`.

2.  Restarts normal pool target background activity.

3.  Marks the pool connectable.

4.  Exposes terminal success through conversion status.

Clients must reconnect after conversion; evicted handles are not restored.

The original submission RPC has already returned. A `dmg` process using `--wait` observes completion through polling; conversion continues if that process exits, loses connectivity, or reaches its wait timeout.

## Failure Handling

-   **Before quiesce completes**: Restart quiesced targets with old geometry and make the pool connectable.

-   **After any META blob grows**: Keep the pool fenced and resume forward. Expanded blobs are not shrunk.

-   **Before the evictable flag is set**: A shard remains phase1-compatible with larger storage. Resume heap/VOS preparation.

-   **After the evictable flag is set**: The shard is phase2 and must roll forward. Startup uses the SMD generation to resume instead of opening the pool for service.

-   **Insufficient tmpfs after META expansion**: Keep the pool fenced. The administrator must free memory capacity or increase the tmpfs allocation before resume.

-   **Management leader change**: The new leader reads the RDB conversion record, queries per-target SMD state, and resumes from the first incomplete stage.

-   **Client or RPC timeout**: A submission timeout is ambiguous, so retrying the same request token returns the existing generation instead of creating another operation. A status timeout has no effect on conversion. Long-running work is never tied to the lifetime of an initiating RPC.

# Implementation Phases

## Phase 1: Control Plane and Quiesce

-   Add asynchronous `dmg pool resize`, `resize-status`, and reversible `resize-cancel` commands, control API types, protobuf messages, management-service handling, and pool-service serialization.

-   Add pool non-connectable state, connection eviction, bounded target stage start/status RPCs, background workers, and distributed progress reporting.

-   Implement dry-run geometry and capacity validation.

## Phase 2: Storage Expansion

-   Add BIO/SPDK META blob resize and SMD size-update APIs.

-   Add idempotent VOS memory-file growth and tmpfs capacity checks.

-   Add DAV2 heap extension for enlarged store and cache geometry.

## Phase 3: Activation and Recovery

-   Update VOS and BIO size fields and activate `META_HDR_FL_EVICTABLE`.

-   Add RDB/SMD conversion records, startup fencing, forward recovery, and cross-target consistency checks.

-   Reopen targets in phase2 and restore pool availability after validation.

# Compatibility & On-disk Impact

## Persistent Layout

No existing allocation layout changes. Conversion updates:

-   DAV2 heap size, cache size, NE percentage, checksum, and zone-information-vector range.

-   BIO META capacity, evictable flag, and checksum.

-   SMD META blob size and conversion state.

-   VOS pool META and memory sizes.

-   Pool-service RDB conversion state.

The implementation may consume reserved fields or introduce versioned extension records for conversion state. Any new persistent field requires the corresponding compatibility bit and version checks.

## Backward Compatibility

-   Unconverted DAV2 phase1 pools continue to operate unchanged.

-   Software without this feature can read neither an in-progress conversion marker nor a committed phase2 geometry safely and must reject the pool.

-   Existing objects remain valid because `vos_obj_p2_df` is already present and their metadata remains in NE zones.

## Interoperability

-   **Mixed Software Versions**: Not supported during conversion.

-   **Partial Target Conversion**: The pool remains unavailable until every target commits.

-   **Pool Downgrade**: Not supported.

-   **Pool Operations During Conversion**: Connect, rebuild, extend, exclude, drain, reintegrate, upgrade, check, and destroy are rejected or serialized behind conversion.

# External Interfaces

-   `dmg pool resize <pool> --mem-ratio <percent> [--ne-reserve <size>] [--dry-run] [--wait[=<timeout>]]`.

-   `dmg pool resize-status <pool>` and reversible `dmg pool resize-cancel <pool>` commands. Neither requires a generation argument because only one resize can be active per pool.

-   New short-lived management-service submit, status, and cancel RPCs and corresponding pool-service RPCs.

-   New bounded target stage start/status RPCs for quiesce, capacity validation, resize/activate, and restart. Long work runs in local durable workers.

-   New BIO API for monotonic META blob expansion.

-   New DAV2 API for offline heap and cache-size extension.

-   Pool query output includes conversion state, effective mem-ratio, META size, and memory-file size.

# Testing & Validation

-   Unit Tests:

    -   Geometry, alignment, minimum added E zone, and NE-reserve calculations.

    -   `dmg` parsing and control/management/pool-service request forwarding.

    -   `--wait` polling, wait timeout without cancellation, duplicate submission tokens, and status formatting.

    -   BIO blob-resize idempotency, SMD ordering, and capacity failures.

    -   DAV2 heap extension with old zones unchanged and appended zones unused.

    -   Conversion state-machine resume from every durable state.

    -   Pool-service leadership change and engine restart while no initiating RPC exists.

-   VOS Tests:

    -   Convert empty, partially used, and completely full phase1 heaps.

    -   Verify every old allocation and object offset is unchanged and remains NE.

    -   Verify post-conversion updates allocate new E buckets only from appended zones.

    -   Exercise GC, aggregation, DTX, iterator, scrubber, and large-object paths after conversion.

-   Functional Tests:

    -   Convert a multi-rank, multi-target pool while unrelated pools continue serving I/O.

    -   Verify new connections are rejected, existing handles are evicted, and no pool I/O occurs during conversion.

    -   Verify client reconnect and data integrity after conversion.

    -   Inject failures before and after blob resize, VOS-file growth, heap extension, activation, and target restart.

    -   Use stage durations longer than all configured RPC timeouts and verify conversion completes through asynchronous status polling.

    -   Verify refusal for DAV1, non-MD-on-SSD, already-phase2, degraded, rebuilding, and insufficient-capacity pools.

-   Performance Criteria:

    -   Conversion time is dominated by SPDK metadata synchronization and target restart, not by the amount of existing VOS metadata.

    -   No metadata-blob-sized data copy is performed.

    -   Unrelated pools remain available throughout conversion.

# Risks, Mitigations and Future Works

## Risks and Mitigations

### 1. META Capacity Fragmentation

* **Risk:** An NVMe device may have enough aggregate free capacity but lack resources to expand every target's META blob.
* **Mitigation:** Validate and reserve all required SPDK clusters before resizing the first shard on the device.

### 2. Insufficient NE Headroom

* **Risk:** A nearly full phase1 heap may consume the enlarged memory reserve soon after conversion.
* **Mitigation:** Use a conservative default reserve, allow an explicit larger `--ne-reserve`, and report NE usage and headroom in dry-run and pool query.

### 3. Partial Distributed Conversion

* **Risk:** Failures can leave targets at different resize or activation stages.
* **Mitigation:** Fence the pool with an RDB generation, journal per-target progress in SMD, make every storage operation idempotent, and recover forward.

### 4. Memory Overcommit

* **Risk:** Slightly enlarging every VOS file can exceed the engine's tmpfs or DRAM budget when several pools are converted.
* **Mitigation:** Validate aggregate engine memory capacity before quiescing and reserve the requested growth for the duration of conversion.

## Future Works

### Existing-Zone Reclassification

A future offline optimization may identify old zones containing only object-private metadata and convert them to E zones. The baseline design deliberately keeps all old zones NE to avoid scanning or rewriting existing object metadata.

### Additional META Expansion

The monotonic BIO and DAV2 extension APIs can later support increasing phase2 metadata capacity without changing the memory-file size, subject to pool-level capacity management and compatible `nemb_pct` limits.