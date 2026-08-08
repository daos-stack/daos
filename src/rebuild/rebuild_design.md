# DAOS Rebuild Subsystem — Technical Design Document (v5)

**Status:** Living document  
**Component:** `src/rebuild/`, `src/object/srv_obj_migrate.c`

## Revision History

| Version | Changes |
|---------|---------|
| v1 | Initial design document |
| v2 | Added cross-module contracts, leader failover, admin state machine, aggregation fence, reint/drain/extend, concurrency model, graceful shutdown, test hardening |
| v3 | Corrected I1 (non-bug, protocol ordering prevents the TOCTOU consequence), removed G5 (prerequisite contradictory), downgraded I2/I3/G3 severity, corrected I4/C3 scope |
| v4 | Normalized issue IDs to single `RB-nn` namespace. Restored all v2 detail lost in v3 rewrite. Added two-track framing (see `rebuild_refactoring_plan_v4.md`). |
| v5 | Comprehensive review and revision: corrected pull-phase/generation semantics, expanded pool-map and terminology definitions (rank/engine, xstream/ULT, rebuild task), state-machine figure (added reclaim window, pre-figure state enumeration), generation management rewrite (PR #18358 same-leader retry bump, scan abort on `rt_global_done`), new data-structure sections (§5.6–§5.10), operational-variant state-transition expansion, reintegration modes (§3.3), epoch boundaries clarification (§2.1), btree lifecycle and progress-counter propagation (§10.5, §10.10), pool-map/query lifecycle integration (§9.5), admin-stop outcomes table, successor-scheduling retry-asymmetry documentation, target disambiguation note. 13 editing sequences, ~94 individual edits across 21 sections. |

---

## 1. Purpose and Scope

This document specifies the functional design of the DAOS **Rebuild** subsystem — the mechanism by which DAOS automatically restores data redundancy after a storage target failure, reintegration, drain, extend, or layout upgrade.

The document is authoritative with respect to:
- The intended behavior of each rebuild operation (`RB_OP_*`).
- The two-plane (leader / target) architecture and its concurrency model.
- The protocol by which targets report completion, and the conditions under which the leader declares a rebuild globally done.
- The retry chain that runs after a rebuild failure.
- The relationship between the Rebuild module and the Migrate module.
- The interaction between rebuild and pool-service leader elections, including the regeneration path.
- The self-heal property gate and its effect on rebuild scheduling.
- The aggregation fence contract that prevents VOS aggregation from crossing the rebuild epoch boundary.
- The admin stop/start state machine and its interaction with the scheduling queue.
- Reintegration, drain, and extend as distinct operational variants with their own semantics.
- Cross-module contracts with container, DTX, checksum, and DFS subsystems.
- Graceful shutdown and module cleanup ordering.

It supersedes the existing `src/rebuild/README.md`, which reflects only the earliest version of the implementation.

---

## 2. Terminology

| Term | Definition |
|------|-----------|
| **Pool map** | The authoritative, persistent versioned map of all storage targets in a pool. Each membership change (target add, exclude, drain, reintegrate) increments `map_ver` sequentially. The map also encodes a fault-domain topology tree used by placement. |
| **Target** | One VOS instance on one DAOS engine. A rank hosts one or more targets. |
| **Rank / Engine** | A rank identifies a DAOS engine process in the CaRT communication group. Each engine hosts one system xstream (XS-0) and multiple VOS xstreams. In this document, "rank" and "engine" are used interchangeably; "rank" is preferred in addressing/identity contexts while "engine" is preferred when discussing execution resources. |
| **Xstream (XS)** | An Argobots execution stream — a cooperatively scheduled thread of execution within a DAOS engine. XS-0 (the system xstream) handles metadata and control-path work (RPCs, leader logic, RPT lifecycle). VOS xstreams (one per VOS target) handle data-path I/O and run scan/migration ULTs (User-Level Threads — lightweight Argobots tasklets that yield cooperatively). |
| **Rebuild task** | A queued or in-flight rebuild operation descriptor on the leader (§5.1). One `rebuild_task` per pending or running `RB_OP_*` operation per pool. Destroyed after `rebuild_task_ult()` completes. |
| **Rebuild operation** | One invocation of the rebuild system for a specific pool map version and op-code. |
| **Leader** | The pool service leader (Raft leader). It orchestrates a rebuild globally. |
| **RGT** (`rebuild_global_pool_tracker`) | Per-pool-per-rebuild state on the leader. Tracks global completion. |
| **RPT** (`rebuild_tgt_pool_tracker`) | Per-pool-per-rebuild state on each target. Tracks local completion. |
| **Migrate TLS** (`migrate_pool_tls`) | Per-pool-per-rebuild per-xstream state in the object migrate module. Holds queued and in-progress migration ULT counters. |
| **IV** (incast variable) | The CaRT incast-variable mechanism used to propagate rebuild status between targets and the leader. |
| **Scan phase** | Each participating target iterates its local VOS to identify objects that must be migrated to restore redundancy. Found objects are enqueued for sending via migration RPCs. |
| **Pull phase** | Historical name for destination-side migration work. In current flow, sources dispatch migration work descriptors/RPCs, while destination migrate ULTs fetch object data from sources and write it into local VOS. |
| **Stable epoch** | The highest epoch at which all in-flight client I/O at the time rebuild started has been committed. Data above this epoch is not migrated. See §2.1 for derivation. |
| **Reclaim epoch** | A saved epoch used by a subsequent `RB_OP_FAIL_RECLAIM` or `RB_OP_RECLAIM` job to discard or confirm migrated data. See §2.1 for derivation. |
| **Generation** (`rebuild_gen`) | Counter used with `(pool_uuid, map_ver)` to identify a rebuild session. It is incremented when the incoming map version exceeds the currently running rebuild version (`ds_rebuild_running_query`). Because the running version resets to 0 after an RGT is torn down, most same-version retries (e.g., network-error retries) also receive a new generation. Additionally, same-leader same-version retries (where the leader rank and term match the running RPT) now explicitly bump generation (PR #18358), covering cases where the predecessor RGT has not yet torn down. The leader-switch path is the primary case where generation is reused for `RB_OP_REBUILD` (RPTs on the new leader keep the running version visible). |
| **`pull_done`** | A per-rank flag on the leader indicating that a target has both finished its local scan and confirmed that all migration work has been received and durably written at its peers. |
| **Rebuild fence** (`spc_rebuild_fence`) | A per-pool-child epoch boundary that prevents VOS aggregation from crossing the rebuild's data boundary. Set at scan start, cleared at rebuild teardown. |
| **Self-heal property** | The pool property (`DAOS_PROP_PO_SELF_HEAL`) controlling automatic rebuild behavior. Bitmask of `DAOS_SELF_HEAL_AUTO_EXCLUDE`, `DAOS_SELF_HEAL_AUTO_REBUILD`, `DAOS_SELF_HEAL_DELAY_REBUILD`. |
| **Reintegration mode** | Pool property (`DAOS_PROP_PO_REINT_MODE`) controlling data movement during target reintegration: `DATA_SYNC` (full copy), `NO_DATA_SYNC` (skip data), or `INCREMENTAL` (delta-only). See §3.3 for detailed mode comparison. |

> **Terminology note — "target" dual usage:** In the rebuild architecture, "target" appears in two senses: (1) a **VOS target** — one storage shard, bound to one VOS xstream; and (2) a **target engine** — a non-leader engine participating in rebuild (as in "leader/target" role split and "Target plane"). Context disambiguates: per-xstream and per-pool-child references mean VOS targets; per-rank and per-engine references (RPT, scan ULT, status ULT) mean target engines. Where ambiguity might arise, the document uses "VOS target" or "target engine" explicitly.

---

### 2.1 Epoch Boundaries and Data-Integrity Model

Rebuild relies on three distinct epoch/time boundaries to maintain data integrity.
Each is computed by a different actor, at a different point in the workflow, and
serves a different correctness purpose. Understanding their relationships is
essential for reasoning about rebuild correctness.

| Boundary | Purpose | When Determined | How Derived | Who Computes | Where Stored | Where Consumed |
|----------|---------|-----------------|-------------|--------------|--------------|----------------|
| **Stable epoch** | Data-visibility cutoff: only data committed at or below this epoch is eligible for migration | During scan broadcast (§7.2) | Each participating target engine snapshots its local HLC in the scan handler (XS-0); the collective aggregator takes the **maximum** across all replies | Scan handler on each target engine; aggregated by collective framework | `rgt_stable_epoch` (leader), `rt_stable_epoch` (target engines, via IV) | Scanner: bounds VOS iteration; clients: writes above this epoch are invisible to rebuild |
| **Rebuild fence** | Aggregation barrier: prevents VOS aggregation from crossing the rebuild's data boundary | During target engine prepare, *before* stable epoch is negotiated | `d_hlc_get()` in `rebuild_tgt_prepare()` on each target engine's XS-0 | Each target engine independently | `rpt->rt_rebuild_fence` (target engine), then copied to `spc_rebuild_fence` on each VOS target (pool child) | VOS aggregation: treats fence as snapshot boundary (§13) |
| **Reclaim epoch** | Cleanup boundary: bounds what reclaim/fail-reclaim will discard or confirm | At scheduling time, before scan begins | `d_hlc_get()` in pool service on the leader engine | Leader engine (pool service) | `rgt_reclaim_epoch` (leader), propagated as `rsi_reclaim_epoch` in scan RPCs | `RB_OP_RECLAIM` / `RB_OP_FAIL_RECLAIM`: epoch below which stale data is discarded |

**Chronology of boundary establishment:**

1. **Reclaim epoch** is seeded first — the leader engine calls `d_hlc_get()` when
   scheduling the rebuild task (in `ds_rebuild_schedule`, pool service).
2. **Rebuild fence** is established next — each target engine takes a local HLC
   snapshot during `rebuild_tgt_prepare()` (on XS-0) and propagates it to every VOS
   target (pool child) on that engine via `rebuild_prepare_one`.
3. **Stable epoch** is negotiated last — during the scan broadcast, each target
   engine's scan handler snapshots its local HLC and returns it in the collective
   reply. The leader aggregates the maximum and distributes it back to target engines
   via IV.

Because HLC advances monotonically: `reclaim_epoch < rebuild_fence < stable_epoch`
(in practice; the ordering is guaranteed by the temporal sequence of `d_hlc_get()`
calls across the workflow).

> **Key distinction:** The rebuild fence is often confused with the stable epoch
> because both appear in the aggregation path, but they are computed at different
> times by different actors. The fence is a *local* per-target-engine value
> established early (during prepare); stable epoch is a *global* collective result
> established later (during scan). The fence prevents aggregation from consuming
> data that rebuild may need to read; stable epoch determines which data rebuild
> actually migrates.


## 3. Rebuild Operations

The rebuild system defines five operations (`daos_rebuild_opc_t`):

| Op-code | Trigger | Purpose |
|---------|---------|---------|
| `RB_OP_REBUILD` | Pool map change (exclude, drain, reintegrate, extend) | Migrate object data to restore the configured redundancy level. |
| `RB_OP_RECLAIM` | Automatic, scheduled after successful `RB_OP_REBUILD` | Reclaim space from stale data on targets that donated data during rebuild. |
| `RB_OP_FAIL_RECLAIM` | Automatic, scheduled after a failed `RB_OP_REBUILD` that had already started scanning | Discard partially migrated data from destination targets to return the pool to a consistent pre-rebuild state. |
| `RB_OP_UPGRADE` | Pool layout version upgrade | Migrate objects to the new layout version across all targets. |
| `RB_OP_NONE` | — | Sentinel; used to indicate "no retry needed". |

### 3.1 Operational Variants Under `RB_OP_REBUILD`

While all pool-map-change-triggered rebuilds use `RB_OP_REBUILD`, the triggering event determines important behavioral differences:

| Trigger | Target State Transition | Key Differences |
|---------|------------------------|-----------------|
| **Exclude** (`PO_COMP_ST_DOWN`) | Typically `UP/UPIN -> DOWN` (map update), then `DOWN -> DOWNOUT` on rebuild completion | Standard rebuild: data migrated from surviving replicas/shards to replacement targets. Subject to `self_heal` property gate. If `DAOS_SELF_HEAL_DELAY_REBUILD` is set, scheduled with `delay_sec = -1` (indefinitely deferred). |
| **Drain** (`PO_COMP_ST_DRAIN`) | `UPIN -> DRAIN`, then `DRAIN -> DOWNOUT` on rebuild completion | Proactive evacuation: data moved off a target before planned removal. The target remains online and serves reads during the process. `DELAY_REBUILD` is not applied to this path, but scheduling still honors common guards such as `sp_disable_rebuild`. |
| **Reintegrate** (`PO_COMP_ST_UP`) | `DOWN/DOWNOUT -> UP -> UPIN` during reintegration rebuild | Target rejoins pool after repair. Behavior depends on the pool's reintegration mode property — see §3.3 for the full lifecycle comparison. `DELAY_REBUILD` is not applied; mode and pool guards still apply. |
| **Extend** | New-target path behaves as `NEW -> UP -> UPIN` when rebuild runs | Data redistributed to include new storage. Internally treated as reintegration (`PO_COMP_ST_UP`) because the pool map already includes the extending targets. `DELAY_REBUILD` is not applied; common scheduling guards still apply. |

> **Design note (from `ds_rebuild_regenerate_task`):** During leader failover, some extend jobs may be regenerated as reintegration jobs. This is intentional and safe because: (1) the pool map already includes the extending targets, and (2) discarding on an empty target is harmless.

### 3.2 The `self_heal` Property Gate

Before scheduling any rebuild, the system evaluates `is_pool_rebuild_allowed()` (`src/include/daos_srv/pool.h`):

```c
static inline bool
is_pool_rebuild_allowed(struct ds_pool *pool, uint64_t self_heal, bool auto_recovery)
{
    bool auto_rebuild_enabled  = self_heal & DAOS_SELF_HEAL_AUTO_REBUILD;
    bool delay_rebuild_enabled = self_heal & DAOS_SELF_HEAL_DELAY_REBUILD;

    if (pool->sp_disable_rebuild)
        return false;
    if (auto_recovery && !(auto_rebuild_enabled || delay_rebuild_enabled))
        return false;
    return true;
}
```

**Interaction with scheduling:**
- `sp_disable_rebuild` (set by fault injection `DAOS_REBUILD_DISABLE` or explicit pool property) blocks all rebuild scheduling.
- `DAOS_SELF_HEAL_AUTO_REBUILD` enables automatic rebuild on exclude events.
- `DAOS_SELF_HEAL_DELAY_REBUILD` allows rebuild scheduling but with `dst_schedule_time = UINT64_MAX` (indefinitely deferred). The task stays in the queue and is only dispatched when a new non-deferred task for the same pool merges with it.
- The gate is evaluated both at initial scheduling time and during `ds_rebuild_regenerate_task()` on leader failover.

**Path nuance:**
- Administrator-initiated pool map operations (`dmg` path) evaluate scheduling with `auto_recovery=false`, so AUTO/DELAY self-heal bits are not the deciding gate in that path.
- Leader-failover regeneration evaluates with `auto_recovery=true`, so system and pool self-heal policy bits are applied when deciding whether to regenerate rebuild tasks.
- In all paths, `sp_disable_rebuild` remains a hard scheduling veto.

**Important caveat (RB-17):** If `self_heal` changes while a task is queued (but not yet dispatched), the queued task retains its original scheduling parameters. There is no mechanism to retroactively apply a changed `self_heal` property to already-queued tasks.


### 3.3 Reintegration Modes

The pool's **reintegration mode** property (`DAOS_PROP_PO_REINT_MODE`, values defined in `daos_prop.h`) controls what happens when an excluded target is reintegrated. The three modes represent different trade-offs:

- **`DAOS_REINT_MODE_DATA_SYNC`** (value 0) — the **default**. Full data synchronization. The returning target is wiped and rebuilt from surviving copies.
- **`DAOS_REINT_MODE_NO_DATA_SYNC`** (value 1) — a **niche / read-only workload** mode. No data movement at all. The target simply rejoins the map. The pool operates at reduced redundancy after reintegration.
- **`DAOS_REINT_MODE_INCREMENTAL`** (value 2) — a **future optimization / preferred direction**. Only data written since the target was excluded is migrated. The returning target keeps its pre-exclusion data intact.

#### Full Lifecycle Comparison

| Aspect | DATA_SYNC (default) | NO_DATA_SYNC | INCREMENTAL |
|--------|---------------------|--------------|-------------|
| **On target exclusion** | Normal rebuild runs: data migrated from surviving replicas/shards to replacement targets | Target immediately marked DOWNOUT — **no rebuild scheduled** (`srv_pool.c:7898–7908`) | Normal rebuild runs (same as DATA_SYNC) |
| **Client I/O during degraded state** | Reads and writes available (degraded mode) | **Writes blocked** (`-DER_NO_PERM` via `srv_obj.c:2151`); reads available | Reads and writes available (degraded mode) |
| **Reintegration step 1: data discard?** | Yes — `pool_discard()` wipes the returning target's stale data | No discard (target was never rebuilt, so nothing to discard) | No discard — returning target retains pre-exclusion data; `pool_recov_cont()` performs container-level recovery |
| **Reintegration step 2: data movement?** | Full rebuild: all object shards rebuilt from surviving sources | No migration — target rejoins map empty, pool at **reduced redundancy** | Delta migration only: uses `global_stable_epoch` (`srv_obj_migrate.c:3455–3472`) to identify data written after exclusion; `reint_post_process_ult` discards stale objects not present in the migrated tree |
| **Pool property constant** | `DAOS_REINT_MODE_DATA_SYNC` (0) | `DAOS_REINT_MODE_NO_DATA_SYNC` (1) | `DAOS_REINT_MODE_INCREMENTAL` (2) |

#### Per-Mode Details

**DATA_SYNC (default):**
Standard lifecycle. On exclusion, a full `RB_OP_REBUILD` runs. On reintegration, the returning target's data is discarded via `pool_discard()`, then a full rebuild copies all required object shards from surviving sources. This is the safest and most thoroughly tested path.

**NO_DATA_SYNC:**
Designed for pools that only serve reads after initial population. On exclusion, the target is immediately marked DOWNOUT and **no rebuild is scheduled** — the pool simply operates at reduced redundancy. A blanket write ban (`-DER_NO_PERM`) is enforced via the object I/O path for all client writes to the pool.

> **Reviewer TODO (RB-18):** The write ban applies even *before* any target failure occurs — as soon as the pool property is set. This is the current implementation behavior (`srv_obj.c:2151` checks `sp_reint_mode == DAOS_REINT_MODE_NO_DATA_SYNC`). Whether this is intentional or a bug requires clarification.

On reintegration, the operation is map-only: no discard, no data migration. The target rejoins with whatever data it had (which may be stale), and the pool remains at reduced redundancy.

**INCREMENTAL:**
On exclusion, a normal rebuild runs (identical to DATA_SYNC). The key difference is at reintegration: instead of discarding and fully rebuilding, the system performs delta-only migration. The returning target retains its pre-exclusion data. Container recovery (`pool_recov_cont()`) re-establishes container metadata, then only data written *after* the exclusion (determined by `global_stable_epoch`) is migrated to the returning target. A post-processing step (`reint_post_process_ult`) removes stale objects on the returning target that are no longer part of the current placement.

> **Reviewer TODO:** Incremental reintegration code paths (`sp_incr_reint`, `mpt_reintegrating`, `reint_post_process_ult`) are less exercised than the DATA_SYNC path. Careful review and additional testing of epoch-boundary edge cases is recommended.

#### DELAY_REBUILD Independence

The reintegration mode property is **independent** of `DAOS_SELF_HEAL_DELAY_REBUILD`. Tests exercise incremental reintegration with immediate rebuild on exclusion (no delay). Both combinations (delayed and immediate rebuild before incremental reintegration) are mechanically supported.

---

## 4. High-Level Architecture

The rebuild system operates in two planes:

```
┌─────────────────────────────────────────────────────────────┐
│  Leader Plane  (runs on pool service leader, XS 0)          │
│                                                             │
│  rebuild_task queue  ──►  rebuild_task_ult()                │
│                               │                             │
│                          rebuild_leader_start()             │
│                               │                             │
│                     rebuild_global_pool_tracker (RGT)       │
│                     ┌─────────────────────────┐             │
│                     │  per-rank scan_done[]   │             │
│                     │  per-rank pull_done[]   │             │
│                     │  is_global_scan_done()  │             │
│                     │  is_global_done()       │             │
│                     └─────────────────────────┘             │
│                               │                             │
│                    rebuild_leader_status_check() loop       │
│                    (polls every 2 s; publishes IV)          │
└──────────────────────────────┬──────────────────────────────┘
                               │  Incast Variable (IV) — bidirectional
         ┌─────────────────────┼─────────────────────┐
         │                     │                     │
┌────────▼──────┐   ┌──────────▼──────┐   ┌─────────▼──────┐
│  Target A     │   │  Target B       │   │  Target C      │
│               │   │                 │   │                │
│  RPT          │   │  RPT            │   │  RPT           │
│  scan ULT     │   │  scan ULT       │   │  scan ULT      │
│  status ULT   │   │  status ULT     │   │  status ULT    │
│               │   │                 │   │                │
│  Migrate TLS  │   │  Migrate TLS    │   │  Migrate TLS   │
│  (per xstream)│   │  (per xstream)  │   │  (per xstream) │
└───────────────┘   └─────────────────┘   └────────────────┘
```

**Leader plane** responsibilities:
- Maintain the rebuild task queue and serialize operations per pool.
- Broadcast `REBUILD_OBJECTS_SCAN` RPCs to all participating targets to start a rebuild.
- Accumulate per-target status updates arriving via IV.
- Declare global scan done and global rebuild done.
- Publish `riv_global_scan_done` / `riv_global_done` IV updates back to all targets.
- Schedule follow-on operations (`RB_OP_RECLAIM`, `RB_OP_FAIL_RECLAIM`, retry).

**Target plane** responsibilities:
- Run the scan ULT to enumerate locally owned objects that must be migrated.
- Dispatch migration RPCs to destination targets.
- Run the status-check ULT to poll local migrate TLS state and report progress to the leader via IV.
- Await `riv_global_done` from the leader before tearing down local state.

**Migrate module** responsibilities (`src/object/srv_obj_migrate.c`):
- Receive migration work descriptors via RPCs (`ds_object_migrate()`).
- Fetch object data from source targets and write it to the local VOS.
- Track in-progress work via per-xstream `migrate_pool_tls` counters.
- Handle checksum propagation: fetch checksums from source via `dcs_iod_csums`, recalculate when necessary via `migrate_csum_calc()`.
- Respect `mpt_reintegrating` flag for incremental reintegration mode (incremental reintegration mode; see §3.3).

---

## 5. Data Structures

### 5.1 `rebuild_task`

Lives on the **leader** only. Represents one pending or running rebuild operation.
Each `RB_OP_*` variant (rebuild, reclaim, fail_reclaim, upgrade) gets its own task.
Destroyed at the end of `rebuild_task_ult()` via `rebuild_task_destroy()`.

| Field | Purpose |
|-------|---------|
| `dst_pool_uuid` | Pool being rebuilt. |
| `dst_map_ver` | Pool map version this operation targets. |
| `dst_rebuild_op` | The operation code (`RB_OP_*`). |
| `dst_tgts` | List of target IDs that triggered this rebuild (for exclude/reint/drain/extend). For `RB_OP_FAIL_RECLAIM`, carries a copy of the original task's target list (merged via `pool_target_id_list_merge`). Empty for reclaim and upgrade. |
| `dst_reclaim_epoch` | Epoch boundary for reclaim. |
| `dst_schedule_time` | Coarse timestamp after which the task may run. `UINT64_MAX` (`REBUILD_SCHEDULE_DELAY_INDEFINITE`) means "indefinitely deferred; merge with next non-deferred task." Set by `DAOS_SELF_HEAL_DELAY_REBUILD` (§3.2). While deferred, the task is only dispatched when a new non-deferred task merges with it (§7.1). |
| `dst_reclaim_ver` | Minimum pool map version of the target list; used as the floor for the subsequent reclaim. |
| `dst_retry_rebuild_op` | The op-code to retry after a `RB_OP_FAIL_RECLAIM` completes. |
| `dst_retry_map_ver` | The map version to use for the retry. |
| `dst_stop_admin` | Populated only when scheduling an `RB_OP_FAIL_RECLAIM`. Indicates the preceding operation (rebuild or fail_reclaim) received an admin stop. No `RB_OP_REBUILD` retry shall follow after the fail_reclaim completes. Always `false` for non-fail_reclaim tasks. See §9.4. |
| `dst_num_op_rb` | Count of tries to run rebuild. |
| `dst_num_op_freclaim` | Count of tries to run fail-reclaim. |
| `dst_num_op_freclaim_fail` | Count of fail-reclaim failures. |

### 5.2 `rebuild_global_pool_tracker` (RGT)

Lives on the **leader** only. Created at the start of a rebuild operation and destroyed when the operation fully completes or is aborted.

| Field | Purpose |
|-------|---------|
| `rgt_pool_uuid` | Pool identity. |
| `rgt_rebuild_ver` | Pool map version for this operation. |
| `rgt_rebuild_gen` | Generation counter distinguishing retries at the same map version. |
| `rgt_leader_term` | Raft term of the leader that started this rebuild. Used to discard stale IV updates from a previous term. |
| `rgt_servers[]` | Array of `rebuild_server_status`, one per rank. Holds per-rank `scan_done`, `pull_done`, `dtx_resync_version`, and heartbeat timestamp. |
| `rgt_servers_sorted[]` | Pointer array into `rgt_servers`, sorted by rank for O(log n) binary search. |
| `rgt_stable_epoch` | Stable epoch negotiated at scan broadcast time. All migration is bounded above by this epoch. See §2.1 for derivation and chronology. |
| `rgt_reclaim_epoch` | Saved for use by the subsequent reclaim operation. See §2.1 for derivation and chronology. |
| `rgt_status` | `daos_rebuild_status`: publicly visible state, progress counters, error code, fail rank. Updated live via the IV reduction callback (`rebuild_iv_ent_update`) as target reports arrive; memcpy'd to the caller at query time. See §5.10 and §10.10. |
| `rgt_abort` | Set to 1 to abort all ULTs and trigger the failure path. |
| `rgt_stop_admin` | Set to 1 when an administrator explicitly stops the rebuild. Always set together with `rgt_abort` (§9.4). |
| `rgt_opc` | The `RB_OP_*` for this tracker. |
| `rgt_init_scan` | Set after the scan broadcast completes. Indicates that remote scan ULTs are running and must be notified on abort. |
| `rgt_dtx_resync_version` | Global minimum DTX resync version across all participating ranks. See §8 for the DTX coordination protocol. |

### 5.3 `rebuild_tgt_pool_tracker` (RPT)

Lives on **every target engine** (XS-0). One RPT per active rebuild per pool per engine.

| Field | Purpose |
|-------|---------|
| `rt_pool_uuid` | Pool identity. |
| `rt_rebuild_ver` | Pool map version for this operation. |
| `rt_rebuild_gen` | Must match the leader's generation for IV updates to be accepted. |
| `rt_rebuild_op` | The operation code. |
| `rt_leader_term` | The leader's Raft term when this RPT was created. Must match the leader's current term for IV updates to be accepted; stale-term IVs are discarded. |
| `rt_global_scan_done` | Set by IV refresh when the leader broadcasts that all targets' scans are complete. This is the gate that makes `pull_done` trustworthy. |
| `rt_global_done` | Set when the leader broadcasts `riv_global_done=1`. Triggers teardown of all local state on this target. |
| `rt_abort` | If set, the status-check ULT and all migration ULTs must stop immediately. |
| `rt_scan_done` | Local flag: set when the local scan ULT has finished enumerating objects. |
| `rt_stable_epoch` | Echo of the leader's stable epoch. Scan and migration do not use data above this epoch. See §2.1 for derivation. |
| `rt_rebuild_fence` | Local VOS epoch fence preventing VOS aggregation from crossing the rebuild boundary. See §2.1 for derivation and §13 for the aggregation fence contract. |
| `rt_global_dtx_resync_version` | Global minimum DTX resync version received from leader IV. See §8 for the DTX coordination protocol. |

### 5.4 `migrate_pool_tls`

Lives in the **Argobots TLS (thread-local storage) of every xstream** on every target. One entry per active rebuild per pool per xstream.

| Field | Purpose |
|-------|---------|
| `mpt_pool_uuid` | Pool identity. |
| `mpt_version` | Pool map version. |
| `mpt_generation` | Generation. The triple `(pool_uuid, version, generation)` uniquely identifies a TLS entry. |
| `mpt_tgt_obj_ult_cnt` | Count of active per-object migration ULTs on this xstream. |
| `mpt_tgt_dkey_ult_cnt` | Count of active per-dkey write ULTs on this xstream. |
| `mpt_ult_running` | 1 if the primary migration scheduling ULT (`migrate_ult`) is running on this xstream. |
| `mpt_fini` | Set to 1 by `ds_migrate_stop()` to signal all ULTs to exit cleanly. Also set to 1 on a per-ULT unrecoverable error. |
| `mpt_status` | Error code from the last failed migration ULT. Propagates to `riv_status` on the IV. |
| `mpt_root_hdl` | In-memory btree of objects waiting to be migrated on the destination side (to-be-migrated tree). Populated by incoming `MIGRATE` RPCs and by the scan-side local-target bypass path (`rebuild_object_local`, see §7.3 case (c)). |
| `mpt_migrated_root_hdl` | In-memory btree keyed by `(container_uuid, oid, epoch, tgt_idx)` of objects already migrated on this xstream. Serves as a deduplication guard: before spawning a migration ULT, the system checks this tree to avoid re-migrating an object that has already been durably written in a previous batch or retry. |
| `mpt_done_eventual` | ABT eventual: signaled when all ULTs have exited and the TLS may be safely destroyed. |
| `mpt_opc` | The rebuild operation code (`RB_OP_*`). |
| `mpt_reintegrating` | Set to 1 for incremental reintegration mode (`sp_incr_reint && opc == RB_OP_REBUILD && tgt_status == PO_COMP_ST_UP`).  See §3.3 for the incremental reintegration lifecycle. |
| `mpt_inflight_size` | Tracks the aggregate size (bytes) of in-flight I/O descriptors (IODs) on this xstream. Used for flow control: migration ULTs yield when this value exceeds a threshold, preventing DMA buffer exhaustion that would stall all I/O on the engine. |

### 5.5 `rebuild_iv`

The wire-format payload exchanged between target and leader via the CaRT incast-variable mechanism.

**Target → Leader (status update, `CRT_IV_SHORTCUT_TO_ROOT`, sync-none):**

| Field | Meaning |
|-------|---------|
| `riv_rank` | Reporting target's rank. |
| `riv_ver` / `riv_rebuild_gen` | Identifies which rebuild this update belongs to. |
| `riv_leader_term` | Leader term; updates from stale terms are discarded. |
| `riv_scan_done` | 1 if this target's local scan is complete. |
| `riv_pull_done` | 1 if this target asserts that all data has been received and written at its peers. Conditional: see §7.3. |
| `riv_status` | Non-zero if an error occurred. Both `scan_done` and `pull_done` can be simultaneously trusted on error. |
| `riv_dtx_resyc_version` | This target's current DTX resync version. The leader tracks the minimum across all ranks. |
| Progress counters | `riv_obj_count`, `riv_rec_count`, `riv_toberb_obj_count`, `riv_size` — delta values per report. See §10.10 for the full propagation chain. |

**Leader → Target (status notification, `CRT_IV_SYNC_LAZY`, sync):**

| Field | Meaning |
|-------|---------|
| `riv_global_scan_done` | 1 when every participating rank has reported `scan_done`. This is the gate for `pull_done` trustworthiness. |
| `riv_global_done` | 1 when every rank has reported `pull_done`, or the operation is aborted. Triggers target teardown. |
| `riv_stable_epoch` | Stable epoch from the leader. |
| `riv_global_dtx_resyc_version` | Global minimum DTX resync version (across all ranks). |

### 5.6 `rebuild_global` (Module Singleton)

Per-engine singleton (`rebuild_gst` in `srv.c`). Owns the task queue, tracker lists, and coordination primitives.

| Field | Purpose |
|-------|---------|
| `rg_queue_list` | Linked list of `rebuild_task` entries waiting to be dispatched. Only one task per pool runs at a time (§7.1). |
| `rg_running_list` | Linked list of `rebuild_task` entries currently executing (at most `REBUILD_MAX_INFLIGHT` = 10). |
| `rg_global_tracker_list` | Linked list of `rebuild_global_pool_tracker` (RGT) entries — one per active rebuild. XS-0 only. |
| `rg_tgt_tracker_list` | Linked list of `rebuild_tgt_pool_tracker` (RPT) entries for rebuilds this engine participates in. Protected by `rg_ttl_rwlock`. |
| `rg_completed_list` | Linked list of recently completed task records (used for query). |
| `rg_ttl_rwlock` | Read-write lock protecting `rg_tgt_tracker_list`. Write lock for insert/delete (XS-0); read lock for non-XS-0 callers. |
| `rg_lock` | Mutex serializing access to the queue and running lists. |
| `rg_stop_cond` | Condition variable signaled when all `rebuild_task_ult` instances exit during `ds_rebuild_leader_abort_all()`. |
| `rg_inflight` | Count of currently active `rebuild_task_ult` instances (≤ `REBUILD_MAX_INFLIGHT`). |
| `rg_rebuild_running` | Boolean: true while at least one rebuild is active on this engine. Consumed by aggregation sleep logic (§12). |
| `rg_abort` | Set to 1 during `ds_rebuild_leader_abort_all()` (graceful shutdown, §15.2). Signals all queued tasks to be discarded. |

### 5.7 `rebuild_tls` (Per-XStream)

Per-xstream thread-local storage for the rebuild module (registered via `dss_tls_register`). One instance per xstream.

| Field | Purpose |
|-------|---------|
| `rebuild_pool_list` | Linked list of `rebuild_pool_tls` entries (§5.9) active on this xstream. Used by scan-side ULTs to track per-pool scan state. |

### 5.8 `rebuild_server_status` (Per-Rank, Within RGT)

Per-rank status entry within `rgt_servers[]`. Tracks each participating rank's progress as observed by the leader.

| Field | Purpose |
|-------|---------|
| `rank` | Rank identifier. |
| `last_update` | Timestamp of the most recent IV update from this rank. Used for slow-engine detection (`update_and_warn_for_slow_engines`). |
| `dtx_resync_version` | Latest DTX resync version reported by this rank. The global minimum is computed across all ranks. |
| `scan_done` | Write-once latch: set to 1 when the rank reports its scan is complete. Pre-set for non-participating ranks. |
| `pull_done` | Write-once latch: set to 1 when the rank reports all migration is durably written. Pre-set for non-participating ranks. |

### 5.9 `rebuild_pool_tls` (Per-XStream Per-Pool, Scan Side)

Per-xstream per-pool scan-side state. Lives in `rebuild_tls.rebuild_pool_list`. Created during scan setup on each VOS-XS; destroyed during `rebuild_tgt_fini`.

| Field | Purpose |
|-------|---------|
| `rebuild_pool_uuid` | Pool identity. |
| `rebuild_tree_hdl` | In-memory btree handle of objects to-be-migrated (populated by `rebuild_scanner`, drained by `rebuild_objects_send_ult`). See §7.3. |
| `rebuild_pool_obj_count` | Count of objects inserted into `rebuild_tree_hdl`. Contributes to the to-be-rebuilt denominator (§10.10). |
| `rebuild_pool_reclaim_obj_count` | Count of objects processed during reclaim/fail_reclaim. |
| `rebuild_pool_ver` | Pool map version for this scan. |
| `rebuild_pool_gen` | Generation for this scan. |
| `rebuild_pool_leader_term` | Leader term for stale-detection. |
| `rebuild_pool_obj_send_pending` | Count of objects dispatched but not yet acknowledged by destination (flow control). |
| `rebuild_pool_status` | Error code propagated from scan failures. |
| `rebuild_pool_scanning` | 1 while the `rebuild_scanner` ULT is still iterating VOS; 0 after completion. Consumed by `dss_rebuild_check_one`. |
| `rebuild_pool_scan_done` | Set to 1 when VOS iteration is finished. Signals `rebuild_objects_send_ult` to drain remaining entries and exit. |

### 5.10 `daos_rebuild_status` (Public Query Result)

Returned by `daos_pool_query()` and `dmg pool query`. Defined in `daos_pool.h`. Represents a point-in-time snapshot of rebuild progress.

| Field | Purpose |
|-------|---------|
| `rs_version` | Pool map version of the in-progress rebuild (0 if idle). |
| `rs_seconds` | Wall-clock seconds elapsed since this rebuild started. |
| `rs_errno` | Non-zero if the rebuild ended in error (e.g., `-DER_OP_CANCELED`, `-DER_STALE`). |
| `rs_state` | One of `DRS_NOT_STARTED`, `DRS_IN_PROGRESS`, `DRS_COMPLETED`. See §6 for the state machine. |
| `rs_done` | Union alias of `rs_state` (deprecated field; provided for backward compatibility). |
| `rs_max_supported_layout_ver` | Maximum object layout version supported by all engines (used by `RB_OP_UPGRADE`). |
| `rs_flags` | Bitmask: `DRF_SELF_HEAL_BUSY` indicates a self-heal pass is running. |
| `rs_fail_rank` | Rank that failed during rebuild (if applicable). |
| `rs_toberb_obj_nr` | Total objects needing rebuild (progress denominator). Accumulated from source-side scan counters. See §10.10. |
| `rs_obj_nr` | Objects migrated so far (progress numerator). Accumulated from destination-side counters. See §10.10. |
| `rs_rec_nr` | Records (extents/single-values) written. |
| `rs_size` | Bytes written. |

---

## 6. Rebuild Operation State Machine

Each rebuild operation transitions through the following states, tracked in `rgt_status.rs_state`:

- `DRS_NOT_STARTED`: no rebuild-related operation is currently running for this pool.
- `DRS_IN_PROGRESS`: active rebuild-related operation running (includes rebuild, reclaim, and fail-reclaim execution windows).
- `DRS_COMPLETED`: the current rebuild workflow (including successor scheduling) has completed successfully.
- `QUEUED` in the figure is scheduler queue status, not an `rs_state` enum value.

```
          ┌──────────────────────────────────────────────────────────────────┐
          │   ds_rebuild_schedule() called                                   │
          ▼                                                                  │
    ┌───────────┐                                                            │
    │ QUEUED    │  Task on rg_queue_list, waiting for scheduling             │
    └─────┬─────┘                                                            │
          │  rebuild_ults() picks up task; map distributed to all targets    │
          ▼                                                                  │
    ┌───────────────┐                                                        │
    │ DRS_IN_PROGRESS│  RGT created; scan broadcast sent; IV loop running   │
    └──────┬──┬──────┘                                                       │
           │  │                                                              │
           │  └─── rgt_abort=1 (pool map change, admin stop, error) ───►────┤
           │                                                                 │
           │  all ranks: scan_done=1 AND pull_done=1                        │
           │  [pool map: ds_pool_tgt_finish_rebuild() advances targets]     │
          ▼                                                                 │
       ┌────────────────────────────────┐                                       │
       │ DRS_IN_PROGRESS               │  RB_OP_RECLAIM running                │
       │ (successor: reclaim)          │                                       │
       └───────────────┬────────────────┘                                       │
           │ reclaim done                                                   │
           ▼                                                                │
       ┌────────────────┐                                                       │
       │ DRS_COMPLETED  │                                                       │
       └────────────────┘                                                       │
                                                                             │
    ◄──────────────────────── failure path ──────────────────────────────────┘
          │
          │  if rgt_init_scan: schedule RB_OP_FAIL_RECLAIM
          │  else: retry or abandon
          │  [pool map: revert for drain/reint/extend; unchanged for exclude]
          ▼
    ┌───────────────────────┐
    │ DRS_IN_PROGRESS       │  RB_OP_FAIL_RECLAIM running
    │ (fail-reclaim)        │
    └──────────┬────────────┘
               │  completes
               ├── dst_retry_rebuild_op != NONE  ──►  re-queue original rebuild
               │   [exclude: real retry; drain/reint/extend: no-op]
               └── dst_stop_admin or NONE         ──►  DRS_NOT_STARTED
                   [pool map: unchanged; targets stay in rebuild-eligible state]
```

**Pool map transitions within the state machine:**

| Transition | Pool map effect | Target state change |
|-----------|-----------------|---------------------|
| Rebuild succeeds | `ds_pool_tgt_finish_rebuild()` | DOWN→DOWNOUT, DRAINING→DOWNOUT, UP→UPIN |
| Failure: non-retryable (exclude) | No revert (DOWN not reverted) | Targets stay DOWN |
| Failure: non-retryable (drain/reint/extend) | `ds_pool_tgt_revert_rebuild()` | DRAINING→UPIN, UP→DOWN/DOWNOUT/NEW |
| Failure: retryable (any) | No change | Targets stay in current state |
| Admin stop (any) | No revert | Targets stay in current state |

---

## 7. Protocol Specification

### 7.1 Phase 0: Scheduling and Queueing

`ds_rebuild_schedule()` is the single public entry point for enqueuing a new rebuild operation. It runs on the pool service leader, XS 0 only.

**Merge rule:** Before creating a new `rebuild_task`, the scheduler searches the existing queue for a task with matching `(pool_uuid, rebuild_op)` and compatible map version. A task may be merged into an existing one if no intermediate operation of a different type sits between them in the queue (see §7.2). Merging prevents O(n²) task proliferation during rapid successive engine/target failures.

**Deferred scheduling:** A task with `dst_schedule_time == REBUILD_SCHEDULE_DELAY_INDEFINITE` (`UINT64_MAX`) is never dispatched directly. It is only eligible to be merged into a subsequent non-deferred task for the same pool. The primary use case for this is with pool property `self_heal:delay_rebuild`: engine/target failures that the administrator quickly fixes and follows with a direct reintegration to subsume the deferred original rebuild. This is also used when an `RB_OP_FAIL_RECLAIM` completes after a stopped rebuild: the original rebuild targets must not be lost, but should be subsumed by a subsequent fresh rebuild triggered by the admin (e.g., `pool rebuild start` or `pool reintegrate` command).

**Serialization rule:** Only one rebuild operation per pool may be in-flight at a time. If a task for pool P is already on `rg_running_list`, any new task for pool P waits on `rg_queue_list`.

**Maximum concurrency:** At most `REBUILD_MAX_INFLIGHT` (= 10) rebuild operations may run concurrently on a given leader engine (across pools for which that engine is currently pool-service leader). This is not a cluster-wide global cap.

**Known issue (RB-12):** When all queued tasks are deferred, the outer loop in `rebuild_ults` sleeps via `dss_sleep(0)` (yield), causing busy-spin on XS-0.

### 7.2 Phase 1: Leader Start (`rebuild_leader_start`)

1. **Generation management.** Query the currently running rebuild via `ds_rebuild_running_query_adv()`, which returns the running version, generation, leader rank, and leader term. Increment `pool->sp_rebuild_gen` if **any** of the following holds:
   - The incoming pool map version exceeds the running version (`version < task->dst_map_ver`).
   - The incoming version equals the running version **and** the current leader rank and term match those stored in the running RPT (`version == task->dst_map_ver && leader_rank == rebuild_leader_rank && leader_term == rebuild_leader_term`). This covers same-leader same-version retries (PR #18358), ensuring a fresh generation even when the predecessor's RGT has not yet torn down.

   Because the running version drops to 0 after any RGT teardown, most same-version retries (including network-error retries on the same leader) also trigger an increment via the first condition. The exception is leader-switch for `RB_OP_REBUILD`: if the new leader's own RPT still holds the previous version with a different rank/term, neither condition fires and the generation is reused, so target TLS entries are not orphaned.

   > **Design invariant:** The triple `(pool_uuid, map_ver, rebuild_gen)` must uniquely identify a rebuild session across the entire cluster lifetime. The mechanism above guarantees uniqueness for higher-version transitions, for same-version retries that follow RGT teardown (where `running_version` resets to 0), and for same-leader same-version retries where the predecessor RPT is still running (PR #18358 explicitly checks leader rank/term match). The remaining exception is the leader-switch path for `RB_OP_REBUILD`: the new leader's own RPT may keep the previous version visible with a different rank/term, suppressing both conditions. If the previous session's `migrate_pool_tls` exists with `mpt_fini=1`, TLS ambiguity can result. See **RB-05**.

   **Scan abort on completion (PR #18358):** When a rebuild session completes or is interrupted, scan ULTs on each target now check `rpt->rt_global_done` (in addition to `rt_abort` and `rt_finishing`) before sending each migration batch and before processing each scanned object. This allows scan threads to exit promptly when the rebuild is no longer needed, rather than continuing to iterate objects until the next status-aggregation cycle detects completion.

2. **RGT allocation.** Create `rebuild_global_pool_tracker` for `(pool, map_ver, gen, leader_term, reclaim_eph, op)`. Allocate `rgt_servers[]` with one slot per rank in the pool map. Ranks that will not participate (DOWN, DOWNOUT, NEW, or UP with `co_in_ver > rebuild_ver`) are pre-marked `scan_done=1, pull_done=1` by the leader status-check loop.

3. **Scan broadcast.** Send `REBUILD_OBJECTS_SCAN` RPC to all participating ranks (broadcast, aggregated). The RPC payload includes:
   - `rsi_rebuild_ver`, `rsi_rebuild_gen`, `rsi_leader_term`
   - `rsi_rebuild_op`
   - `rsi_reclaim_epoch` (used by reclaim/fail-reclaim to bound the discard)
   - `rsi_master_rank`

   The RPC is a collective with a co-op handler (`rebuild_tgt_scan_co_ops`). Each target's handler (`rebuild_tgt_scan_handler`) creates an RPT and spawns two ULTs:
   - the **scan ULT** (`rebuild_scan_leader`, which distributes `rebuild_scanner` to each VOS-XS)
   - the **status-check ULT** (`rebuild_tgt_status_check_ult`)

   The broadcast reply returns a `rso_stable_epoch` computed as the **maximum** across all successfully responding target engines (each target engine's scan handler snapshots its local HLC). The leader stores this in `rgt->rgt_stable_epoch`.

4. **Leader monitoring.** Launch `rebuild_leader_status_check()`, the leader's polling loop (§7.5).

### 7.3 Phase 2: Object Scan (Per-Target)

The scan ULT runs on each participating target, on the system XS (XS 0) for each pool child.

1. **EC aggregation pause.** Before beginning VOS iteration, the scanner calls `ds_cont_child_wait_ec_agg_pause()` to ensure EC aggregation is paused. See §13 for the aggregation fence contract. **RB-03:** The timeout on this call is currently swallowed (returns `void`); see §18.

2. **DTX orphan cleanup.** Calls `dtx_cleanup_orphan(pool_uuid, sp_dtx_resync_version)` to resolve any uncommitted distributed transactions that survived the DTX resync phase. See §8 for DTX coordination.

3. **VOS iteration.** `rebuild_scanner` (one per VOS-XS, spawned via `ds_pool_thread_collective` inside `rebuild_scan_leader`) iterates all locally owned objects using `vos_iterate` → `rebuild_container_scan_cb` → `rebuild_obj_scan_cb`. For each object, placement is recalculated against the new pool map to determine what action is needed. Three cases arise:

   **(a) Typical case — remote redundancy restoration.** The object shard on this target remains in place, but the loss of another target means a peer must receive a copy to restore the object's redundancy group. The scanner inserts the affected OID into the per-xstream `rebuild_tree_hdl` btree (in `struct rebuild_pool_tls`). A companion send ULT (step 4) will later dispatch a `MIGRATE` RPC to the chosen destination engine.

   **(b) Placement-driven relocation to a remote target.** In some operations (reintegration, drain), the placement algorithm determines that *this* target is no longer the correct home for the shard — even though the target itself is healthy. The shard must move elsewhere. This also results in an insert into `rebuild_tree_hdl` and a subsequent `MIGRATE` RPC to the new home.

   **(c) Local-target bypass.** If the new placement assigns the shard to a different target on the *same* engine (same rank, different target index), the scanner calls `rebuild_object_local()` directly instead of inserting into `rebuild_tree_hdl`. This spawns a ULT on the destination VOS-XS and calls `ds_migrate_object()` inline — no RPC, no network hop — feeding into the destination's `mpt_root_hdl` tree (see §10.5).

4. **Object send ULT (producer/consumer).** Before beginning VOS iteration, `rebuild_scanner` spawns `rebuild_objects_send_ult` on the same VOS-XS (`DSS_XS_SELF`). The two ULTs cooperate via cooperative yielding on the single-threaded xstream:
   - The scanner (producer) inserts OIDs into `rebuild_tree_hdl` and yields every `SCAN_YIELD_CNT` placement-cost units.
   - The send ULT (consumer) drains `rebuild_tree_hdl`, batching up to `REBUILD_SEND_LIMIT` OIDs per call, and invokes `ds_object_migrate_send()` to dispatch `MIGRATE` RPCs to destination engines. It yields (`dss_sleep`) when the tree is temporarily empty.

5. **Scan complete (two-step).** After VOS iteration finishes:
   - (a) `rebuild_pool_scan_done = 1` — signals the send ULT that no more objects will be inserted.
   - (b) `ABT_thread_free(&ult_send)` — joins (blocks) on the send ULT, which drains any remaining entries from `rebuild_tree_hdl` and exits.

   This ordering guarantees that all `MIGRATE` RPCs have been sent **and acknowledged** by destination engines before `rebuild_scanner()` returns. (`ds_object_migrate_send` uses `dss_rpc_send`, which blocks until the remote reply is received.)

   > **Design note:** The remote acknowledgment means the destination has received and enqueued the object descriptors into its local `mpt_root_hdl` tree — it does **not** mean the destination has completed data fetch and durable write. The gap between RPC acceptance and durable write completion is a destination-side concern; see INV-2 (§11) for the corresponding completion-tracking invariant. See §7.8 for how per-xstream scan completion propagates to the leader.

### 7.4 Phase 3: Status Reporting (Per-Target, Polling Loop)

The status-check ULT (`rebuild_tgt_status_check_ult`) polls every `RBLD_CHECK_INTV` (2,000 ms) and reports status to the leader via IV.

**Each iteration:**

1. Call `rebuild_tgt_query(rpt, &status)`:
   - Snapshot `global_scan_done = rpt->rt_global_scan_done` before querying migrate state.
   - Call `ds_migrate_query_status(pool, ver, gen, op, global_scan_done, &dms)` to aggregate ULT counts from every xstream's `migrate_pool_tls`.
   - Call `dss_rebuild_check_one` across xstreams to aggregate the scanning flag.
   - Compute `status.rebuilding`:
     - `true` if `!global_scan_done || status.scanning || dms.dm_migrating`
     - `false` otherwise

2. Set `iv.riv_scan_done = 1` if local scan is complete (`status.scanning == 0`) or the rebuild is aborted or has errored.

3. **Pull-done gate.** Set `iv.riv_pull_done = 1` only when:
   ```
   (rpt->rt_global_scan_done == true)
   AND (status.rebuilding == false)
   ```
   OR the rebuild is aborted (`rpt->rt_abort`).

   The `rt_global_scan_done` gate is critical: `pull_done` must not be reported until every target has finished scanning, because objects scanned later by other targets may still be in-flight to this target.

   **RB-01:** The abort path sets `pull_done=1` without checking `dm_migrating` or waiting for in-flight migration to drain.

4. Send IV to leader: `rebuild_iv_update(..., CRT_IV_SHORTCUT_TO_ROOT, CRT_IV_SYNC_NONE)`.

5. If `rpt->rt_global_done || rpt->rt_abort`: break out of loop, call `rebuild_tgt_fini()`.


> See §7.8 for the full multi-level status tracking picture.
### 7.5 Phase 4: Global Status Aggregation (Leader Polling Loop)

The leader's `rebuild_leader_status_check` loop polls every `RBLD_CHECK_INTV` and:

1. Reads the pool map to pre-mark non-participating ranks as `scan_done=1, pull_done=1`.

2. Checks for reason to abort (new pool map change invalidating this rebuild).

3. Calls `rebuild_leader_status_notify()` to push `riv_global_scan_done` and `riv_global_done` to all targets via `CRT_IV_SYNC_LAZY`.

4. Checks `is_rebuild_global_done(rgt)`: if true, sets `rs_state = DRS_COMPLETED` and exits the loop.

5. Logs slow-rank warnings via `update_and_warn_for_slow_engines()`.

**IV reduction at the leader** (`rebuild_global_status_update`):

When a target IV arrives at the leader, the reduction logic is:

```
1. Record the arrival timestamp for the rank (heartbeat).
2. If iv.riv_scan_done == 0:
       — Update DTX resync version for this rank only.
       — Return.  (Nothing else can be trusted yet.)
3. Mark SCAN_DONE for this rank.
4. Re-evaluate is_rebuild_global_scan_done() AFTER marking this rank done.
5. If global scan is NOT yet done AND iv.riv_status == 0:
       — Return.  (pull_done cannot be trusted: other targets are still scanning
                   and may send more objects to this rank.)
6. If iv.riv_pull_done == 1:
       — Mark PULL_DONE for this rank.
```

> **Protocol ordering guarantee (added in v3):** The IV protocol has an inherent ordering property: a target sets `riv_pull_done=1` ONLY when `rt_global_scan_done=1` (or `rt_abort=1`). `rt_global_scan_done` is set ONLY via the leader's IV notification. The leader sends `global_scan_done=1` ONLY when `is_rebuild_global_scan_done()` returns true. `scan_done` per rank is a write-once latch.
>
> **Consequence:** When the leader processes an IV with `riv_pull_done=1`, `is_rebuild_global_scan_done()` is guaranteed to be true. The guard at step 5 is structurally unreachable for IVs carrying `pull_done=1` in the non-abort case. This means the TOCTOU in the current implementation — where step 4 occurs *before* step 3 — does not cause data loss. Reordering is an optional optimization (saves one 2s cycle for the last rank), not a correctness fix. See §18 Validated Non-Bugs.
>
> This ordering does **NOT** hold for the `rt_abort` path, which is why **RB-01** is a real bug.

> See §7.8 for how `is_rebuild_global_scan_done` gates `pull_done`.

### 7.6 Phase 5: Teardown

When `rpt->rt_global_done` is set (by IV refresh from leader):

1. The status-check ULT breaks its polling loop.
2. `rebuild_tgt_fini(rpt)` is called:
   a. Waits for all in-progress ULTs holding an `rpt` reference (`rt_refcount > 1`).
   b. Destroys `rebuild_pool_tls` on XS 0.
   c. Calls `dss_task_collective(rebuild_fini_one, rpt, 0)` across all VOS xstreams: destroys per-xstream `rebuild_pool_tls`, resets `spc_rebuild_fence`, updates `spc_rebuild_end_hlc = d_hlc_get()`.
   d. Calls `ds_migrate_stop(pool, ver, gen)`:
      - Broadcasts `migrate_fini_one_ult` to every xstream.
      - Each xstream sets `mpt->mpt_fini = 1` on its TLS entry.
      - Waits on `mpt_done_eventual` until all in-progress migration ULTs have exited.
      - Calls `migrate_pool_tls_destroy()`: frees in-memory trees, closes pool/container handles.
   e. Destroys the RPT.

### 7.7 Phase 6: Successor Scheduling (Leader) (`rebuild_task_complete_schedule`)

After the leader loop exits, the task's successor is determined:

```
                    ┌── rebuild succeeded? ─────────────────────────────►
                    │                                                    │
is_global_done &&   │                                     schedule RB_OP_RECLAIM
errno == 0          │                                     (to reclaim stale data)
                    │
                    └── rebuild failed or stopped?
                             │
                             ├── rgt_init_scan == 0  ──►  retry_rebuild_task()
                             │                             (revert map, reschedule)
                             │
                             └── rgt_init_scan == 1  ──►  schedule RB_OP_FAIL_RECLAIM
                                                          (discard partial migration)
                                       │
                                       └── after fail_reclaim completes:
                                                │
                                                ├── dst_retry_rebuild_op != NONE
                                                │   ──►  reschedule original rebuild
                                                │
                                                └── dst_stop_admin
                                                    ──►  DRS_NOT_STARTED (no retry)
```

**RB-04:** In the success branch, the return value from `ds_pool_tgt_finish_rebuild` is not checked. `obj_reclaim_ver` may be set to a wrong value.

Success, revert, and retry outcomes are summarized below:

| Condition | Immediate action | Pool map effect | Resulting status state |
|-----------|------------------|-----------------|------------------------|
| `RB_OP_REBUILD`/`RB_OP_UPGRADE` succeeds | Schedule `RB_OP_RECLAIM` | Targets advanced (DOWN→DOWNOUT, UP→UPIN, etc.) | `DRS_IN_PROGRESS` while reclaim runs; `DRS_COMPLETED` after reclaim completes |
| Rebuild fails before scan init (`rgt_init_scan == 0`) | `retry_rebuild_task()` | Revert for drain/reint/extend; no change for exclude | Typically remains `DRS_IN_PROGRESS` through retry chain |
| Rebuild fails after scan init (`rgt_init_scan == 1`) | Schedule `RB_OP_FAIL_RECLAIM` | Revert for drain/reint/extend; no change for exclude | `DRS_IN_PROGRESS` while fail-reclaim runs |
| Fail-reclaim completes with retry op (exclude) | Reschedule rebuild — effective retry | No change (targets still DOWN) | `DRS_IN_PROGRESS`; error status overwritten on completion |
| Fail-reclaim completes with retry op (drain/reint/extend) | Reschedule rebuild — no-op (targets already reverted) | Already reverted at failure time | `DRS_COMPLETED` with prior `rs_errno` preserved (no-op retry exits before update) |
| Stop-admin latched during rebuild or fail-reclaim | Do not retry original rebuild | No revert (targets stay as-is) | `DRS_NOT_STARTED`; `rs_errno = -DER_OP_CANCELED` |

**Exclude vs. drain/reint/extend retry asymmetry:** The retry after fail-reclaim has fundamentally different behavior depending on the operation type. For `exclude`, the DOWN targets remain in the pool map, so the retried rebuild finds real work and runs to completion (overwriting the error status on the completed list). For `drain`, `reintegrate`, and `extend`, the map revert during failure handling removes the rebuild targets from eligibility. The retried rebuild therefore finds no targets, returns 0 from `rebuild_leader_start()`, and exits immediately — a no-op. Because this no-op path skips `rebuild_task_complete_schedule()`, it never calls `rebuild_status_completed_update()`, and the original failure's error status (`rs_errno`, `rs_state = DRS_COMPLETED`) persists on the completed list.

---
### 7.8 Scan and Status Tracking: System-Level View

The following diagram summarizes how scan-done status propagates from individual VOS-xstream ULTs to the global leader, crossing four coordination levels:

```
Level 1: Per-VOS-XS (one per xstream per engine)
──────────────────────────────────────────────────────────────────────────
  rebuild_scanner[xs_i]
      │
      ├── VOS iteration complete
      ├── rebuild_pool_scan_done = 1  (signals send ULT)
      ├── ABT_thread_free(send_ult)   (join: waits for tree drain)
      └── return  ────────────────────►  ds_pool_thread_collective callback returns

Level 2: Engine coordination (rebuild_scan_leader, on sys-XS)
──────────────────────────────────────────────────────────────────────────
  rebuild_scan_leader
      │
      ├── ds_pool_thread_collective(rebuild_scanner)
      │       ← blocks until ALL VOS-XS callbacks return
      ├── All xstreams done on this engine
      └── rebuild_pool_tls (XS-0):  rebuild_pool_scanning = 0

Level 3: Engine ──► Leader reporting (rebuild_tgt_status_check_ult, polling)
──────────────────────────────────────────────────────────────────────────
  rebuild_tgt_status_check_ult  (polls every 2 s)
      │
      ├── rebuild_tgt_query() aggregates:
      │       scanning = dss_rebuild_check_one across xstreams
      │       migrating = ds_migrate_query_status across xstreams
      ├── If scanning == 0 → set iv.riv_scan_done = 1
      ├── If rt_global_scan_done AND !rebuilding → set iv.riv_pull_done = 1
      └── rebuild_iv_update() → IV pushed to leader (CRT_IV_SHORTCUT_TO_ROOT)

Level 4: Global (leader's rebuild_global_status_update / rebuild_leader_status_check)
──────────────────────────────────────────────────────────────────────────
  rebuild_global_status_update (IV arrival handler)
      │
      ├── Record scan_done for this rank
      ├── is_rebuild_global_scan_done()?
      │       All participating ranks have scan_done == 1
      │       → rgt->rt_global_scan_done = 1
      └── After global_scan_done:
              Accept pull_done arrivals

  rebuild_leader_status_check (polls every 2 s)
      │
      ├── rebuild_leader_status_notify() → push global_scan_done / global_done via IV
      └── is_rebuild_global_done()?
              All participating ranks have pull_done == 1
              → DRS_COMPLETED, exit loop
```

**Temporal ordering (happy path):**

1. Each VOS-XS finishes its `rebuild_scanner` → all MIGRATE RPCs acknowledged.
2. `ds_pool_thread_collective` returns → `rebuild_pool_scanning = 0` for this engine.
3. Next `rebuild_tgt_status_check_ult` poll (≤ 2 s) → `riv_scan_done = 1` sent to leader.
4. Leader receives scan_done from all ranks → `rt_global_scan_done = 1`.
5. Leader pushes `global_scan_done` via IV to all targets (≤ 2 s cycle).
6. Targets receive `rt_global_scan_done`, wait for `dm_migrating == 0` → `riv_pull_done = 1`.
7. Leader receives pull_done from all ranks → `DRS_COMPLETED`.

**Why `rt_global_scan_done` gates `pull_done`:**

A target may finish its own scan early and have zero in-flight migrations — but other targets are still scanning and may at any moment dispatch `MIGRATE` RPCs *to* this target. If the target reported `pull_done=1` at that point, the leader could declare `DRS_COMPLETED` while objects are still in flight. The `rt_global_scan_done` gate ensures that `pull_done` is only trusted after every target has finished sending (step 4 → 5 above).


## 8. DTX Resync Coordination

Distributed Transaction (DTX) resync ensures that any uncommitted in-flight transactions are resolved before rebuild migrates their data. The protocol is:

1. Each target's status-check ULT reports its current `sp_dtx_resync_version` in `riv_dtx_resyc_version`.
2. The leader computes the global minimum across all participating ranks (excluding ranks with version skip sentinel `UINT32_MAX`), stored in `rgt_dtx_resync_version`.
3. The leader broadcasts the global minimum via `riv_global_dtx_resyc_version` on each IV notification.
4. On receipt, each target updates `rpt->rt_global_dtx_resync_version` and, if it now equals or exceeds the local `rt_rebuild_ver`, signals `rt_global_dtx_wait_cond` to wake the scan ULT that may be blocked waiting for DTX sync.

A target's scan ULT does not begin migration until its local DTX resync has reached the rebuild version. The scan leader (`rebuild_scan_leader` in `scan.c:1083`) implements this as a `while` loop:

```c
while (rpt->rt_global_dtx_resync_version < rpt->rt_rebuild_ver) {
    ABT_cond_wait(rpt->rt_global_dtx_wait_cond, rpt->rt_lock);
    if (rpt->rt_abort || rpt->rt_finishing)
        goto out;
}
```

**DTX orphan cleanup.** Before beginning the VOS scan, each target calls `dtx_cleanup_orphan()` (`scan.c:1011`) to resolve DTX entries whose coordinator crashed before the transaction committed or aborted. This prevents rebuild from migrating uncommitted data.

**Cross-module contract:** DTX resync is performed by the DTX module (`src/dtx/`), not by rebuild. Rebuild depends on:
- `sp_dtx_resync_version` being updated by the DTX resync ULT.
- `dtx_cleanup_orphan()` correctly resolving all orphaned entries before returning.
- Any changes to DTX resync semantics (e.g., batching, lazy cleanup) must not delay the version update past the point where rebuild needs it.

---

## 9. Failure Handling

### 9.1 Target Failure During Rebuild

If a target fails (detected by SWIM) while a rebuild is in-progress:

1. A new pool map version is created, marking the target DOWN.
2. The leader's `rebuild_leader_status_check` loop detects `PO_COMP_ST_DOWN` with `co_fseq > rgt_rebuild_ver`: sets `rgt_abort = 1`, `rs_errno = -DER_STALE`.
3. The failure path runs: `RB_OP_FAIL_RECLAIM` is scheduled if any scan had started.
4. After fail-reclaim: the failing target's new DOWN status generates a new `RB_OP_REBUILD` at the new map version.

**Pool map outcome:** For the *interrupted* rebuild, the pool map behavior depends on operation type:
- **Exclude (original target failure):** No pool map revert. Targets remain DOWN. The retry at the new map version will rebuild data for both the original failed target(s) and the newly failed target.
- **Drain/reint/extend:** Pool map is reverted (`ds_pool_tgt_revert_rebuild()`). This resets the in-progress drain/reint/extend targets to their pre-operation state. A new rebuild for the cascading failure is scheduled at the new map version.

### 9.2 Leader Failure During Rebuild

1. The Raft leader switch increments the leader term.
2. The new leader calls `ds_rebuild_regenerate_task()` (`srv.c:2715`), which scans the pool map for targets in DOWN, DRAIN, and UP states and regenerates the corresponding rebuild tasks.

   **Regeneration logic** (`ds_rebuild_regenerate_task`):
   ```
   For each component state:
     PO_COMP_ST_DOWN → Generate RB_OP_REBUILD task
                        (subject to self_heal gate; if DELAY_REBUILD, delay_sec = -1)
     PO_COMP_ST_DRAIN → Generate RB_OP_REBUILD task (always immediate)
     PO_COMP_ST_UP → Generate RB_OP_REBUILD task for reintegration (always immediate)
   ```

   **Critical property (RB-08):** `auto_recovery=true` is passed for leader-change regeneration, meaning the `self_heal` property is evaluated. If `sp_disable_rebuild` was set or `DAOS_SELF_HEAL_AUTO_REBUILD` was cleared between the original scheduling and the leader switch, the regeneration will skip the rebuild — potentially leaving the pool degraded. Additionally, the `NO_DATA_SYNC` check incorrectly blocks all operations, not just reintegration. See §3.3 for the full impact of `NO_DATA_SYNC` on rebuild scheduling.

   **Scheduling-path contrast:** Unlike administrator-initiated operations (§3.2 "Path nuance"), leader-failover regeneration passes `auto_recovery=true`. This means the `DAOS_SELF_HEAL_AUTO_REBUILD` and `DAOS_SELF_HEAL_DELAY_REBUILD` bits are consulted. A pool whose self-heal policy changed between the original scheduling and the failover may not have its rebuild regenerated. The `sp_disable_rebuild` veto applies unconditionally in both paths.

3. The new leader calls `rebuild_leader_start()`. Because `version >= task->dst_map_ver` is satisfied by the existing running rebuild, `generation` is **not** incremented (same-version case). **RB-05:** This means the stale `migrate_pool_tls` with `mpt_fini=1` may be reused.
4. The existing RPTs on targets remain alive (matched by the unchanged `(pool, ver, gen)` triple). The new leader's scan broadcast re-arms them.
5. The old leader's IV updates are discarded by the new leader because `src_iv->riv_leader_term != rgt->rgt_leader_term`.

   **State transitions during leader failover:**
   ```
   Old Leader                         New Leader
   ──────────                         ──────────
   rebuild_task_ult running     →     ds_rebuild_leader_abort_all()
   rgt_abort = 1               ←     (clears rg_abort, creates new task)
   IV discarded (term mismatch) →     ds_rebuild_regenerate_task()
   ULTs drain and exit          →     rebuild_leader_start()
                                →     scan broadcast (re-arms RPTs)
                                →     rebuild_leader_status_check() loop
   ```

   > **Potential orphan window:** Between the old leader aborting and the new leader's scan broadcast arriving, target status-check ULTs continue sending IVs to the old leader's rank. These are silently dropped at the CaRT level (no listener). If a target completes its scan and reports `pull_done` during this window, the report is lost. The new leader's fresh scan broadcast resets the status tracking, so correctness is maintained but work may be repeated.

   **RB-09:** `dst_retry_rebuild_op`, `dst_retry_map_ver`, and `rgt_stop_admin` are in-memory only — lost on leader failover.

### 9.3 Network Errors

Transient network errors (`-DER_TIMEDOUT`, `-DER_GRPVER`, `-DER_STALE`, `-DER_CRT_*`) cause `rgt_abort = 1`. The task is rescheduled at the same map version via `retry_rebuild_task()`.

**Pool map effect:** Retryable errors do NOT trigger `ds_pool_tgt_revert_rebuild()` — the pool map is left unchanged. The retry runs against the same pool map state.

**Error status on retry:** Because the retry runs as a new rebuild task that goes through the full `rebuild_task_complete_schedule()` path on success, the completed list entry is overwritten with `rs_errno = 0` on success — the transient error does not persist in the query response.

**Generation behavior on network-error retry:** The abort tears down the current RGT, resetting `running_version` to 0 on this leader. When `rebuild_leader_start()` runs for the retried task, `version > running_version` evaluates to true, and generation IS incremented. This gives each network-error retry a fresh `(pool_uuid, map_ver, rebuild_gen)` triple, avoiding stale `migrate_pool_tls` reuse.

This contrasts with leader-switch retries (§9.2), where the RPT on the new leader keeps `running_version` at the previous value, suppressing the increment.

**RB-06:** IV notification loss using `CRT_IV_SYNC_LAZY`. The leader retries notification every 2s in its polling loop, providing automatic recovery for transient loss. Prolonged delay only under persistent network failure.

### 9.4 Administrative Stop/Start

#### 9.4.1 Admin Stop (`ds_rebuild_admin_stop`)

`ds_rebuild_admin_stop()` may be called by a `dmg pool rebuild stop` command:

- Permitted for `RB_OP_REBUILD` and `RB_OP_UPGRADE`.
- Accepted during `RB_OP_FAIL_RECLAIM` without `force`: returns success, sets `rgt_stop_admin=1`, but does **not** abort the running fail-reclaim. On completion, `check_to_retry_orig_rebuild()` clears `dst_retry_rebuild_op`, so the original rebuild is not retried. Final state: `DRS_NOT_STARTED`.
- Permitted for `RB_OP_FAIL_RECLAIM` with `force=1` only if previous fail-reclaim invocations have failed. Aborts the running fail-reclaim immediately.
- **Not permitted for `RB_OP_RECLAIM`** — returns `-DER_BUSY`. **RB-13:** No log message explains why.

**Admin stop state machine:**

```
ds_rebuild_admin_stop() called
    │
    ├── RGT not found for pool?
    │       └── return -DER_NONEXIST
    │
    └── RGT found:
            │
            ├── rebuild_is_stoppable(rgt, force)?
            │       │
            │       ├── Queued task exists for same pool
            │       │       └── return -DER_NO_PERM
            │       │
            │       ├── RB_OP_REBUILD / RB_OP_UPGRADE
            │       │       └── return true (stoppable)
            │       │
            │       ├── RB_OP_FAIL_RECLAIM + force=1 + rgt_num_op_freclaim_fail > 0
            │       │       └── return true (stoppable)
            │       │
            │       ├── RB_OP_FAIL_RECLAIM + force=1 + rgt_num_op_freclaim_fail == 0
            │       │       └── return false (defer stop; rc=0)
            │       │
            │       ├── RB_OP_FAIL_RECLAIM + force=0
            │       │       └── return false (defer stop; rc=0)
            │       │
            │       └── RB_OP_RECLAIM
            │               └── return false; rc = -DER_BUSY
            │
            ├── If stoppable: rgt_abort=1, rs_errno=-DER_OP_CANCELED
            │
            └── Unconditionally, if (rgt_abort || opc == RB_OP_FAIL_RECLAIM):
                    rgt_stop_admin = 1
```

**Effect of `rgt_stop_admin` on fail-reclaim completion:**

- When fail-reclaim finishes, `check_to_retry_orig_rebuild()` sees `rgt_stop_admin=1` and clears `dst_retry_rebuild_op` — the original rebuild is not retried. Final state: `DRS_NOT_STARTED`, `rs_errno = -DER_OP_CANCELED`.

**Pool map effect of admin stop:** Admin stop does NOT revert the pool map. The code path through `rebuild_task_complete_schedule()` with `rgt_stop_admin=1` sets `opc = RB_OP_NONE` and returns before reaching `retry_rebuild_task()` (which is where pool map revert lives). As a result:
- Targets that were being drained remain in DRAINING state.
- Targets being reintegrated remain in UP (not UPIN) state.
- The administrator must run `dmg pool rebuild start` (which calls `ds_rebuild_admin_start` → `ds_rebuild_regenerate_task`) to resume. Re-issuing the original command (e.g., `dmg pool drain` or `dmg pool reintegrate`) would fail because the targets are already in the intermediate state (DRAINING or UP).

**Error status preservation:** The error `rs_errno = -DER_OP_CANCELED` and state `DRS_COMPLETED` are written to the completed list via `rebuild_status_completed_update_partial()`. These persist in pool query responses until a subsequent successful rebuild for the same or higher map version overwrites the entry.

**Summary of outcomes by case:**

| Running op | force | Condition | Immediate effect | Final outcome |
|-----------|-------|-----------|-----------------|---------------|
| RB_OP_REBUILD/UPGRADE | — | — | `rgt_abort=1`, `rgt_stop_admin=1` | Abort, then fail-reclaim, then no retry |
| RB_OP_FAIL_RECLAIM | 0 | — | `rgt_stop_admin=1` (no abort) | Fail-reclaim continues; on completion, retry cleared |
| RB_OP_FAIL_RECLAIM | 1 | freclaim_fail > 0 | `rgt_abort=1`, `rgt_stop_admin=1` | Abort fail-reclaim immediately |
| RB_OP_FAIL_RECLAIM | 1 | freclaim_fail == 0 | `rgt_stop_admin=1` (no abort) | Same as force=0 |
| RB_OP_RECLAIM | — | — | return `-DER_BUSY` | No effect |
| (queued task for pool) | — | — | return `-DER_NO_PERM` | No effect |
| (no RGT) | — | — | return `-DER_NONEXIST` | No effect |

#### 9.4.2 Admin Start (`ds_rebuild_admin_start`)

`ds_rebuild_admin_start()` (`srv.c:2781`) is called by `dmg pool rebuild start`:

1. Fetches current pool properties via IV.
2. Calls `ds_rebuild_regenerate_task()` with `auto_recovery=false` and `delay_sec=0`.
3. This scans the pool map for DOWN/DRAIN/UP targets and creates immediate rebuild tasks.

> **Note:** Admin start does not clear `sp_disable_rebuild`. If the pool was disabled via fault injection, admin start will fail at the `is_pool_rebuild_allowed()` gate. The `sp_disable_rebuild` flag must be cleared separately.

### 9.5 Pool Query: Rebuild Status at Each Lifecycle Phase

`ds_rebuild_query()` (`srv.c:829`) is the entry point for pool query's rebuild status section. It synthesizes the current rebuild state from two sources:

1. **Active RGT:** If a `rebuild_global_pool_tracker` exists for the queried pool, its `rgt_status` is returned directly (showing `DRS_IN_PROGRESS` with live progress counters).
2. **Completed list:** If no RGT exists, the most recent entry on `rg_completed_list` matching the pool UUID is returned (showing `DRS_COMPLETED` with final counters).
3. **Override rule:** Even when the completed list would show `DRS_COMPLETED`, if tasks for the pool exist on `rg_running_list` or `rg_queue_list`, the state is overridden to `DRS_IN_PROGRESS`.

The following table shows what pool query returns at each lifecycle phase:

| Lifecycle phase | `rs_state` | `rs_errno` | Pool map state | Notes |
|----------------|-----------|-----------|----------------|-------|
| Idle (no rebuild history) | `DRS_NOT_STARTED` | 0 | Stable | No RGT, no completed list entry |
| Queued (waiting for slot) | `DRS_IN_PROGRESS` | 0 | Trigger committed | Override rule (#3): task on queue → in-progress |
| Scan/pull active | `DRS_IN_PROGRESS` | 0 | Unchanged from trigger | Live from RGT |
| Reclaim running | `DRS_IN_PROGRESS` | 0 | Targets already advanced | RGT exists for reclaim op |
| Completed successfully | `DRS_COMPLETED` | 0 | Targets in final state | From completed list |
| Failed (non-retryable, exclude) | `DRS_COMPLETED` | non-zero | Targets still DOWN | Error preserved initially; overwritten after successful retry |
| Failed (non-retryable, drain/reint/extend) | `DRS_COMPLETED` | non-zero | Targets reverted | Error persists permanently (no-op retry never overwrites) |
| Failed (retryable) | `DRS_IN_PROGRESS` | 0 | Unchanged | Retry is immediately scheduled; override rule applies |
| Admin-stopped | `DRS_COMPLETED` | `-DER_OP_CANCELED` | Targets not reverted | Written via `rebuild_status_completed_update_partial()` |
| Fail-reclaim running | `DRS_IN_PROGRESS` | 0 | Revert already applied (if applicable) | Fail-reclaim runs as a new RGT; original error is on completed list but shadowed by active RGT |
| Retry scheduled (post-fail-reclaim) | `DRS_IN_PROGRESS` | 0 | Depends on op type | Override rule: new task on queue |

**Error status persistence rules (post-PR #18204):**

When a rebuild fails with a non-retryable error, `rgt_status.rs_state` is set to `DRS_COMPLETED` (rather than remaining `DRS_IN_PROGRESS`), and `rs_errno` records the failure code. This entry is written to the completed list. The subsequent behavior depends on operation type:

- **Exclude:** The retry after fail-reclaim finds real work (targets still DOWN). On successful completion, `rebuild_status_completed_update()` overwrites the completed list entry with `rs_errno = 0`. The error is transient in the query response.
- **Drain/reintegrate/extend:** Pool map revert removes the targets from rebuild eligibility. The retry calls `rebuild_leader_start()` which returns 0 (no targets). The task exits via `goto output`, bypassing `rebuild_task_complete_schedule()` entirely. The completed list is never overwritten, so `rs_errno` from the original failure persists indefinitely in pool query responses.
- **Admin stop:** The error `-DER_OP_CANCELED` persists until the administrator runs `dmg pool rebuild start` and the regenerated rebuild completes successfully.

---

## 10. Concurrency Model

### 10.1 XStream Taxonomy

A DAOS engine is a single OS process hosting multiple **Argobots xstreams** — each of which is a native OS thread running a cooperative (non-preemptive) ULT scheduler. All rebuild work runs as ULTs within these xstreams; there are no additional OS threads created by the rebuild module itself.

The engine exposes the following named xstream types, defined in `src/include/daos_srv/daos_engine.h`:

| Constant | Value | Count per engine | Hosts |
|----------|-------|-----------------|-------|
| `DSS_XS_SYS` | 3 | 1 | Pool service, RDB, rebuild orchestration, DRPC. Referred to as **XS-0** throughout this document. |
| `DSS_XS_VOS` | 0 | V (one per NVMe device / VOS target) | VOS I/O, object scan, data migration. Referred to as **VOS-XS[i]**. |
| `DSS_XS_IOFW` | 1 | 1 | I/O forwarding for TX coordinator. |
| `DSS_XS_OFFLOAD` | 2 | 1 | EC/checksum/compress compute offload. |
| `DSS_XS_SWIM` | 4 | 1 | SWIM failure detection. |

Rebuild uses only XS-0 and the V VOS xstreams. `DSS_XS_SELF` (`-1`) is not a real type; it is a convenience value meaning "create the ULT on the same xstream the caller is running on."

In a typical 4-NVMe-per-engine deployment, one engine has **5 xstreams**: 1 × XS-0, 4 × VOS-XS.

---

### 10.2 Leader-Plane ULTs (XS-0)

These ULTs run on XS-0 of the pool service leader engine. Because XS-0 is cooperative, they share it without any rebuild-internal lock.

| ULT function | Count | Creation site | Lifetime | Responsibility |
|---|---|---|---|---|
| `rebuild_ults` | 1 per process | `ds_rebuild_schedule` via `dss_ult_create(..., DSS_XS_SELF, ...)` called from XS-0 | Exists until rebuild global state is torn down | **Task scheduler.** Loops over `rg_queue_list`, checking pool-map-change preconditions. When a task is ready, dequeues it, increments `rg_inflight`, and spawns one `rebuild_task_ult`. Sleeps 5 s if all tasks are deferred (RB-12). |
| `rebuild_task_ult` | 1 per active rebuild task | `rebuild_ults` via `dss_ult_create(..., DSS_XS_SELF, 0, DSS_DEEP_STACK_SZ, ...)` | Duration of one rebuild task | **Leader lifecycle.** Sends `REBUILD_OBJECTS_SCAN` RPCs to all participating targets, then polls `rebuild_leader_status_check` every 2 s until `is_rebuild_global_done()` is true. On completion, calls `rebuild_task_complete_schedule` to enqueue the next task in the chain (`RB_OP_RECLAIM` or `RB_OP_FAIL_RECLAIM`). |

> **Note:** When a pool service leader switch occurs, the incoming leader engine creates a fresh `rebuild_task_ult` to take over the in-progress rebuild. The outgoing leader's ULT exits cleanly on the next `rgt_abort` IV.

---

### 10.3 Target-Plane ULTs on XS-0

On **every** participating engine (not only the leader), the following ULTs run on XS-0. They are created by the `rebuild_tgt_scan_handler` RPC handler, which itself executes on XS-0 (CaRT dispatches pool-service module RPCs to `DSS_XS_SYS`).

| ULT function | Count | Creation site | Lifetime | Responsibility |
|---|---|---|---|---|
| `rebuild_tgt_status_check_ult` | 1 per active rebuild per engine | `rebuild_tgt_scan_handler` via `dss_ult_create(..., DSS_XS_SELF, 0, DSS_DEEP_STACK_SZ, ...)` | Duration of rebuild on this target | **Status reporter.** Polls every `RBLD_CHECK_INTV` (2,000 ms). At each cycle: queries all VOS xstreams for `dm_migrating` and `dm_status` via `ds_migrate_query_status`; aggregates into an IV; sends the IV to the leader via `CRT_IV_SHORTCUT_TO_ROOT + CRT_IV_SYNC_NONE`. Asserts `riv_pull_done` when `rt_global_scan_done && !dm_migrating`. |
| `rebuild_scan_leader` | 1 per active rebuild per engine | `rebuild_tgt_scan_handler` via `dss_ult_create(..., DSS_XS_SELF, ...)` | Duration of scan phase | **Scan fan-out coordinator.** Waits for DTX resync to reach `rt_rebuild_ver`. Then calls `ds_pool_thread_collective(rebuild_scanner, ...)` which distributes one `rebuild_scanner` invocation to every VOS-XS simultaneously (blocks on XS-0 until all complete). Follows up with `ds_pool_task_collective(rebuild_scan_done, ...)` to clear `rebuild_pool_scanning` on each VOS-XS. |

---

### 10.4 Target-Plane Per-VOS-XStream ULTs (Source Side, Scan Module)

`rebuild_scan_leader` fans out work to every VOS-XS via `ds_pool_thread_collective`. The following ULTs therefore run once per VOS xstream per active rebuild on each participating engine.

| ULT function | XStream | Count | Creation | Responsibility |
|---|---|---|---|---|
| `rebuild_scanner` | Each VOS-XS[i] | 1 per active rebuild | `ds_pool_thread_collective` inside `rebuild_scan_leader` | **VOS iterator.** Pauses EC aggregation (`ds_cont_child_wait_ec_agg_pause`), cleans up orphan DTX entries, creates the per-TLS `rebuild_tree_hdl`, and then calls `vos_iterate` to enumerate all containers, objects, dkeys, and recxs in the local VOS pool. For each object that needs migrating to a remote target, inserts the OID into `rebuild_tree_hdl`. For objects that can be migrated locally, calls `rebuild_object_local` directly. |
| `rebuild_objects_send_ult` | Same VOS-XS[i] as its `rebuild_scanner` | 1 per scanner (not created for `RB_OP_RECLAIM` / `RB_OP_FAIL_RECLAIM`) | `rebuild_scanner` via `dss_ult_create(..., DSS_XS_SELF, ...)` | **RPC dispatcher.** Runs concurrently with the scanner on the same VOS-XS. Continuously drains `rebuild_tree_hdl` by iterating containers and objects, batching up to `REBUILD_SEND_LIMIT` OIDs per call, and invoking `ds_object_migrate_send` to dispatch migration RPCs to destination engines. Exits when `rebuild_pool_scan_done == 1` and the tree is empty. |
| `rebuild_obj_ult` | VOS-XS[tgt_index] of the destination target | 1 per object (local migration only) | `rebuild_object_local` via `dss_ult_create(..., DSS_XS_VOS, tgt_index, ...)` | **Local object mover.** Used only when the source and destination are on the same engine (intra-engine shard rebalance). Calls `ds_migrate_object` inline, inserting the object into the destination target's `migrate_pool_tls` tree. |

---

### 10.5 Target-Plane Per-VOS-XStream ULTs (Destination Side, Migrate Module)

When a migration RPC arrives at a destination engine, `ds_object_migrate` (in `src/object/srv_obj_migrate.c`) is called on the VOS-XS handling that RPC. It creates or looks up a `migrate_pool_tls` for the pool/version/generation tuple, then launches the following ULTs. All use `DSS_XS_SELF`, so they inherit the VOS-XS of the RPC handler.

Certain administrative operations (`ds_migrate_prepare_ult`, `cont_fetch_start_ult`, `cont_fetch_end_ult`, `ds_migrate_end_ult`) use `dss_ult_execute(..., DSS_XS_SYS, ...)` — they are synchronous blocking calls dispatched to XS-0 for pool-service operations such as `ds_pool_lookup` and IV-based container-property fetching.

| ULT function | XStream | Count | Lifetime | Responsibility |
|---|---|---|---|---|
| `migrate_ult` | VOS-XS[i] (same as RPC handler) | 1 per `migrate_pool_tls` (one per VOS-XS per active rebuild) | Until `mpt_root_hdl` drained and `mpt_fini == 1` | **Migration consumer.** Iterates `mpt_root_hdl` (the tree of objects queued for migration) via `migrate_cont_iter_cb`. For each container and object entry, spawns a `migrate_obj_ult`. Runs until the tree is empty or `mpt_fini == 1`. Sets `mpt_ult_running = 0` on exit; the next incoming RPC may spawn a new `migrate_ult` if new objects arrive after the previous one exited. |
| `migrate_obj_ult` | VOS-XS[i] (same as `migrate_ult`) | 1 per object being migrated | Duration of one object migration | **Object migrator.** Opens the source pool/container/object via the DAOS client API (network round-trips, with `dss_sleep(0)` yields during waits). Iterates snapshots and epoch ranges to call `migrate_one_epoch_object`, which decomposes each range into dkey batches and builds `migrate_one` work items. Propagates status to `mpt_status` on failure. Respects `mpt_fini` at entry; **RB-02:** currently returns `0` on early exit at 7 sites. |
| `migrate_one_ult` | VOS-XS[i] (same as `migrate_obj_ult`) | 1 per dkey batch | Duration of one dkey write | **Innermost data writer.** Receives a fully assembled `migrate_one` struct (containing pre-fetched dkey data and IODs). Calls `migrate_dkey` to write the data to the local VOS. This is the only ULT that directly modifies VOS storage on the destination. |


#### Migration Object Trees: Lifecycle

The migrate module maintains two in-memory btrees per `migrate_pool_tls` to track
migration work. Both share a two-level nested structure:

| Level | Key | Value | btree class |
|-------|-----|-------|-------------|
| Root | `uuid_t` (container UUID) | `struct tree_cache_root` (sub-tree handle) | `DBTREE_CLASS_UV` |
| Container sub-tree | `daos_unit_oid_t` (unit OID) | `struct migrate_obj_val` (epoch, punched_epoch, shard, tgt_idx) | `DBTREE_CLASS_NV` |

**`mpt_root_hdl` — To-Be-Migrated Tree:**

| Operation | Function | Thread | Description |
|-----------|----------|--------|-------------|
| **Create** | `migrate_try_create_object_tree()` | VOS-XS of incoming `REBUILD_OBJECTS` RPC | Created on the first migration RPC arrival via `ds_obj_migrate_handler`. Both trees are created together. |
| **Insert** | `migrate_try_obj_insert()` | VOS-XS of RPC handler | Called for each OID in the RPC payload. Checks both trees for duplicates (`-DER_EXIST`) before inserting. |
| **Re-insert** | `migrate_try_obj_insert()` (from `migrate_obj_iter_cb`) | `migrate_ult` on owning VOS-XS | If `migrate_object()` fails, the OID is re-inserted for retry. |
| **Consume** | `migrate_cont_iter_cb()` → `migrate_obj_iter_cb()` | `migrate_ult` on owning VOS-XS | Each OID is deleted from the tree (`dbtree_iter_delete`), then passed to `migrate_object()`. |
| **Destroy** | `migrate_pool_tls_destroy()` → `obj_tree_destroy()` | `migrate_fini_one_ult` dispatched to owning VOS-XS | Bulk destruction of all remaining entries at teardown. |

**`mpt_migrated_root_hdl` — Already-Migrated Tree (Deduplication Guard):**

| Operation | Function | Thread | Description |
|-----------|----------|--------|-------------|
| **Create** | `migrate_try_create_object_tree()` | VOS-XS of incoming RPC | Created together with `mpt_root_hdl`. |
| **Insert** | `migrate_object()` → `obj_tree_insert()` | `migrate_ult` on owning VOS-XS | Immediately after spawning `migrate_obj_ult`. This is the **only** insertion point — an OID moves here as it leaves `mpt_root_hdl`. |
| **Lookup (dedup)** | `migrate_try_obj_insert()` → `obj_tree_lookup()` | VOS-XS of incoming RPC | If an OID is not found in `mpt_root_hdl`, the migrated tree is checked. If found, the OID is a duplicate and is silently dropped. |
| **Lookup (reint)** | `reint_post_process_ult()` → `obj_tree_lookup_uoid()` | `reint_post_process_ult` on owning VOS-XS | During incremental reintegration post-processing: if a local VOS object is NOT found in this tree, it was not migrated and is stale → `vos_obj_delete()`. |
| **Destroy** | `migrate_pool_tls_destroy()` → `obj_tree_destroy()` | `migrate_fini_one_ult` on owning VOS-XS | Bulk destruction. No individual item removals occur — the tree accumulates all migrated OIDs for the duration of the rebuild. |

**Threading invariant:** All operations on both trees run on the same owning VOS-XS.
No cross-xstream access occurs, so no locks are needed. This is guaranteed by the
per-xstream TLS design (see §10.7 XS Access Rules).

**OID lifecycle across both trees:**

```
ds_obj_migrate_handler()  [VOS-XS of RPC]
    │
    ├─ migrate_try_create_object_tree()    ← creates mpt_root_hdl + mpt_migrated_root_hdl
    │
    └─ migrate_try_obj_insert()            ← for each OID in RPC:
         ├─ lookup mpt_root_hdl            ← already queued? → skip
         ├─ lookup mpt_migrated_root_hdl   ← already migrated? → skip
         └─ insert into mpt_root_hdl       ← enqueue for migration

migrate_ult()  [owning VOS-XS, background]
    └─ iterate mpt_root_hdl → migrate_cont_iter_cb() → migrate_obj_iter_cb()
         ├─ dbtree_iter_delete(OID)                   ← remove from mpt_root_hdl
         └─ migrate_object()
              ├─ obj_tree_insert(mpt_migrated_root_hdl)  ← mark as migrated
              └─ spawn migrate_obj_ult()               ← actual data transfer
                   └─ (on failure) re-insert into mpt_root_hdl for retry

migrate_pool_tls_destroy()  [migrate_fini_one_ult on owning VOS-XS]
    ├─ obj_tree_destroy(mpt_root_hdl)          ← destroy any remaining entries
    └─ obj_tree_destroy(mpt_migrated_root_hdl) ← destroy accumulated dedup set
```

---

### 10.6 Thread Count Estimation

For a rebuild involving **E** engines each with **V** VOS xstreams (targets), with **1** pool service leader:

| Category | Count |
|---|---|
| `rebuild_ults` (leader) | 1 |
| `rebuild_task_ult` (leader) | 1 per active rebuild task (typically 1) |
| `rebuild_tgt_status_check_ult` (all engines) | E |
| `rebuild_scan_leader` (all engines) | E |
| `rebuild_scanner` (all engines, all VOS-XS) | E × V |
| `rebuild_objects_send_ult` (all engines, all VOS-XS) | E × V |
| `migrate_ult` (destination engines, all VOS-XS) | ≤ E × V (one per active TLS) |
| `migrate_obj_ult` (destination VOS-XS) | Up to `mpt_tgt_obj_ult_cnt` per VOS-XS |
| `migrate_one_ult` (destination VOS-XS) | Up to `mpt_tgt_dkey_ult_cnt` per VOS-XS |

In a representative 8-engine × 4-target-per-engine pool, the rebuild module creates **~80 simultaneously-live ULTs at peak** (excluding object-level and dkey-level ULTs, which are bounded by the `MIGR_OBJ` and `MIGR_KEY` resource semaphores inside `migrate_pool_tls`).

> **Key design invariant:** All ULTs touching RGT fields run on XS-0. All ULTs touching a given `migrate_pool_tls` run on the same VOS-XS. No mutex is needed in either case — cooperative scheduling guarantees mutual exclusion within an xstream. The only cross-xstream shared state is `rebuild_gst.rg_tgt_tracker_list` (guarded by `rg_ttl_rwlock`) and the `migrate_query_arg` aggregator (guarded by `status_lock`).
>
> **RB-07:** In practice, only `rpt_insert` has the `D_ASSERT(xs_id == 0)` assertion; most other RGT mutation sites lack it.

---

### 10.7 XS Access Rules

| Data structure | Permitted accessors | Protection mechanism |
|---------------|--------------------|--------------------|
| `rebuild_gst.rg_global_tracker_list` | XS-0 only | No lock required; `D_ASSERT(xs_id == 0)` enforced at all callsites |
| `rebuild_gst.rg_queue_list`, `rg_running_list` | XS-0 only | No lock required |
| `rebuild_gst.rg_tgt_tracker_list` | XS-0 for insert/delete; any XS for read | `rg_ttl_rwlock`: write lock for insert/delete; read lock for non-XS-0 callers |
| `rebuild_tgt_pool_tracker` fields | XS-0 primarily; `rt_refcount` from any XS | `rt_lock` mutex for refcount; individual fields accessed only from XS-0 without lock |
| `migrate_pool_tls` fields | Owning VOS-XS only | No lock required (per-xstream) |
| `migrate_query_arg` aggregation across xstreams | Multiple VOS-XS concurrently | `status_lock` mutex |

### 10.8 ABT Cooperative Scheduling

Within a single Argobots xstream (non-preemptive), yield points occur at:
- `sched_req_sleep()` / `dss_sleep()` (2,000 ms polling interval for status loops)
- `ABT_cond_wait()` (DTX sync wait, fini wait)
- `ABT_thread_yield()` (explicit yield in scan and send loops, every `SCAN_YIELD_CNT` = 128 iterations)
- CaRT RPC send/receive (implicit yield while awaiting network completion)

Because xstreams are cooperative, **no two ULTs on the same xstream run simultaneously**. A ULT holds the xstream exclusively until it reaches one of the above yield points. Lock contention is only relevant for state shared across xstreams.

The 2,000 ms sleep in `rebuild_tgt_status_check_ult` defines the IV reporting granularity. The worst-case delay between an event (scan completion, abort, error) and the leader observing it is one `RBLD_CHECK_INTV` cycle (2 s).

### 10.9 IV Callback Threading

`rebuild_global_status_update()` is called from the IV reduction callback (`rebuild_iv_ent_update`), which CaRT invokes when an IV update is reduced at the root. In the current CaRT configuration the IV root handler runs on the pool service leader's XS-0, so in practice this callback always executes on XS-0. However, rebuild code must not rely on this, since CaRT does not formally guarantee it. The caller (`rebuild_iv.c`) holds no rebuild-internal lock when invoking the callback.


### 10.10 Progress Counter Propagation

Migration progress is tracked by four counters that flow from per-xstream state
through engine-level aggregation, delta-encoded IV updates, and leader-side
accumulation. The result is the publicly visible `daos_rebuild_status` that
users and administrators query via `dmg pool query`.

#### Per-Xstream Counters

**Destination side (migrate module, per `migrate_pool_tls`):**

| Counter | Incremented by | When | Meaning |
|---------|---------------|------|---------|
| `mpt_obj_count` | `migrate_obj_ult()` | After an object's full migration completes (final epoch processed) | Objects successfully migrated (progress numerator) |
| `mpt_rec_count` | `migrate_dkey()` | After each dkey fetch+update round | Records (extents/single-values) written |
| `mpt_size` | `migrate_dkey()` | Same as above | Bytes written to local VOS |

**Source side (scan module, per `rebuild_pool_tls` on each VOS-XS):**

| Counter | Incremented by | When | Meaning |
|---------|---------------|------|---------|
| `rebuild_pool_obj_count` | `rebuild_object_insert()` | Each time the scanner identifies an object needing migration and inserts it into `rebuild_tree_hdl` | Objects to-be-rebuilt (progress denominator) |

#### Data Flow Chain

```
Level 1: Per-VOS-XS counters (incremented during scan/migration)
──────────────────────────────────────────────────────────────────────────
  Source VOS-XS:       rebuild_pool_obj_count++  (scanner enumerates object)
  Destination VOS-XS:  mpt_obj_count++, mpt_rec_count++, mpt_size++  (migration writes)

Level 2: Engine-wide aggregation (rebuild_tgt_query, on XS-0, every 2s poll)
──────────────────────────────────────────────────────────────────────────
  ds_migrate_query_status()
      └─ migrate_check_one()  [ds_pool_thread_collective across all VOS-XS]
            Sums: mpt_obj_count → dm_obj_count
                  mpt_rec_count → dm_rec_count
                  mpt_size      → dm_total_size

  dss_rebuild_check_one()  [ds_pool_thread_collective across all VOS-XS]
      Sums: rebuild_pool_obj_count → status.tobe_obj_count

Level 3: Delta computation + IV send (rebuild_tgt_status_check_ult, XS-0)
──────────────────────────────────────────────────────────────────────────
  iv.riv_obj_count        = status.obj_count     - rpt->rt_reported_obj_cnt
  iv.riv_rec_count        = status.rec_count     - rpt->rt_reported_rec_cnt
  iv.riv_size             = status.size           - rpt->rt_reported_size
  iv.riv_toberb_obj_count = status.tobe_obj_count - rpt->rt_reported_toberb_objs

  rebuild_iv_update(..., CRT_IV_SHORTCUT_TO_ROOT, CRT_IV_SYNC_NONE)
  On success: update watermarks (rt_reported_obj_cnt, etc.)

Level 4: Leader accumulation (rebuild_iv_ent_update, IV on_put callback, XS-0)
──────────────────────────────────────────────────────────────────────────
  rgt_status.rs_toberb_obj_nr += riv_toberb_obj_count   (denominator)
  rgt_status.rs_obj_nr        += riv_obj_count           (numerator)
  rgt_status.rs_rec_nr        += riv_rec_count
  rgt_status.rs_size          += riv_size
```

#### Counter Semantics

- **`rs_toberb_obj_nr`** (to-be-rebuilt objects): Accumulates as scanners across all
  engines enumerate objects needing migration. This is the progress *denominator*.
  It increases monotonically during a rebuild session and stabilizes once all
  scanners complete.
- **`rs_obj_nr`** (migrated objects): Accumulates as destination engines complete
  object migrations. This is the progress *numerator*. At all times during a
  rebuild: `rs_obj_nr ≤ rs_toberb_obj_nr`.
- **Progress ratio:** `rs_obj_nr / rs_toberb_obj_nr` provides the user-visible
  rebuild progress percentage. Both values are monotonically increasing (deltas
  are always ≥ 0). The denominator may still be growing while the numerator
  advances, so progress percentage can temporarily decrease.
- **`rs_rec_nr`** and **`rs_size`**: Finer-grained progress indicators (records
  and bytes), but not used for the primary progress percentage.

#### Timing

The status-check ULT polls every `RBLD_CHECK_INTV` (2,000 ms). Delta encoding
means each IV carries only the *new* progress since the last successful report.
If an IV send fails (transient network error), the delta accumulates and is sent
in the next successful cycle — no progress is lost.

See §7.4 for the full status-reporting protocol and §7.8 for the system-level
status tracking diagram.

---

## 11. The Completion Protocol: Correctness Invariants

These invariants describe the *correct* behavior of the subsystem. The current implementation violates several of them; each violation corresponds to a bug cataloged in §18. Invariants that are fully enforced today are noted; the rest are the targets that the Track 1 refactoring (see `rebuild_refactoring_plan_v4.md`) aims to achieve.

### INV-1: Pull-done completeness

**A target may only report `pull_done=1` when ALL of the following are true:**
- (a) `rt_global_scan_done == true` — **enforced today**
- (b) `status.rebuilding == false` — **enforced today**
- (c) all migration RPCs have been received and durably written at the destination — **not yet implemented**

The current design does not satisfy part (c) because:
- The source scanner's completion (send ULT joined) only guarantees dispatch.
- The destination's `dm_migrating == 0` only guarantees that no ULT is currently executing inside the migrate TLS.
- The transit window between dispatch and TLS arrival is unaccounted.

**Required mechanism:** An inflight-RPC counter tracked from dispatch to delivery. A target may only report `pull_done` when `rt_inflight_count == 0`.

Additionally, the abort path currently sets `pull_done=1` without checking `dm_migrating` at all (**RB-01**).

### INV-2: Cancellation propagation

**When `mpt_fini=1`, every early-exit returns `-DER_CANCELED`, not `0`.**

Silent early-exit causes objects to be dropped without any error reaching `riv_status`, preventing the leader from triggering fail-reclaim. **RB-02:** 7 of 10+ check sites in `srv_obj_migrate.c` violate this.

### INV-3: Generation uniqueness on retry

**The triple `(pool_uuid, map_ver, rebuild_gen)` must uniquely identify a rebuild session.** Network-error retries on the same leader naturally satisfy this: the RGT teardown resets `running_version` to 0, triggering a generation increment on the retry. Same-leader same-version retries where the predecessor RPT is still running are now also covered: PR #18358 adds an explicit leader-rank/term match condition that bumps generation even before RGT teardown completes. Leader-switch retries for `RB_OP_REBUILD` may still violate this: the new leader's RPT can keep `running_version` at the previous value with a different rank/term, suppressing both increment conditions. If the previous session's `migrate_pool_tls` exists with `mpt_fini=1`, stale TLS ambiguity results. **RB-05:** Not implemented for leader-switch same-version retries.

### INV-4: EC aggregation safety

**EC aggregation must be paused for the duration of the scan phase. Timeout must abort, not proceed.** A rebuild that proceeds with live EC aggregation may produce persistently corrupt parity cells. **RB-03:** `ds_cont_child_wait_ec_agg_pause()` returns `void`; timeout swallowed.

### INV-5: Aggregation fence lifecycle

**`spc_rebuild_fence` must not be cleared until partial data is either committed (success) or discarded (fail-reclaim).** **RB-10:** Fence cleared immediately on abort, before fail-reclaim.

### INV-6: Container destruction grace

**Container destruction during rebuild produces `-DER_NONEXIST` at both scan and migration paths. The error is handled (skip container), not propagated as rebuild failure.** Partially implemented today.

### INV-7: Reclaim version correctness

**`obj_reclaim_ver` must reflect the actual reclaim boundary. The return value of `ds_pool_tgt_finish_rebuild` must be checked.** **RB-04:** Return value unchecked in current implementation.

---

## 12. Erasure Coding (EC) Rebuild

EC rebuild has additional constraints beyond replica rebuild:

1. **Shard ordering.** A parity shard can only be reconstructed after all data shards for that stripe are available at their new locations. The `DAOS_REBUILD_WAIT_EC_PAUSE` environment variable (read into `rebuild_wait_ec_pause`) allows insertion of a wait between data-shard migration and parity reconstruction.

2. **Partial-stripe handling.** If a stripe is partially filled, the EC update must reconstruct the partial shard correctly. This requires careful coordination between the migration RPC payload and the VOS write path.

3. **Scan ordering.** The scanner must enumerate full stripes before partial ones to avoid parity being rebuilt with stale data.

4. **EC aggregation pause.** The scanner calls `ds_cont_child_wait_ec_agg_pause()` before beginning VOS iteration. This ensures EC aggregation is not running concurrently with the scan, which could compute parity over partially-migrated stripes. **RB-03:** Timeout not propagated.

5. **EC aggregation sleep during rebuild.** Even outside the scan phase, EC aggregation ULTs sleep for 18 seconds between rounds when `ds_pool_is_rebuilding()` returns true (`srv_target.c:543`), reducing interference. VOS aggregation continues normally but respects the `spc_rebuild_fence`.

The interaction between `rebuild_wait_ec_pause` and the EC encode/rebuild path in the migrate module is a known area that requires dedicated testing. Tests in `src/tests/suite/daos_rebuild_ec.c` exercise some cases but do not cover all partial-stripe configurations.

---

## 13. Aggregation Fence Contract

The aggregation fence is a critical correctness mechanism that prevents VOS aggregation from crossing the epoch boundary established by rebuild.

> For the definitions and chronological relationships of rebuild fence, stable epoch, and reclaim epoch, see §2.1.

### 13.1 Fence Setup

When `rebuild_prepare_one` runs on each VOS xstream during scan start:
```c
dpc->spc_rebuild_fence = rpt->rt_rebuild_fence;
```

`rt_rebuild_fence` is a separate HLC timestamp taken during target engine prepare
(on XS-0 via `d_hlc_get()` in `rebuild_tgt_prepare`) and then copied into each VOS
target's `spc_rebuild_fence` by `rebuild_prepare_one`. This value is **not** the
stable epoch — it is established before stable epoch negotiation occurs. See §2.1
for the relationship between rebuild fence, stable epoch, and reclaim epoch.

### 13.2 Fence Effect on VOS Aggregation

In `cont_aggregate_runnable` (`srv_target.c:390`):
```c
if (cont->sc_pool->spc_rebuild_fence != 0) {
    uint64_t rebuild_fence = cont->sc_pool->spc_rebuild_fence;
    /* Insert rebuild_fence into the snapshot list */
    /* Aggregation treats the fence as a snapshot boundary —
     * it will not aggregate across this epoch */
}
```

Additionally, if `spc_rebuild_end_hlc` is set (after a previous rebuild completed) and the aggregation's full scan start HLC is older, aggregation performs a force-scan from epoch 0 to ensure all data is correctly handled.

### 13.3 Fence Teardown

In `rebuild_fini_one` (`srv.c:2828`):
```c
if (rpt->rt_rebuild_fence == dpc->spc_rebuild_fence) {
    dpc->spc_rebuild_fence = 0;
    dpc->spc_rebuild_end_hlc = d_hlc_get();
}
```

**Correctness requirement:** The fence must not be cleared if a new rebuild has started between this rebuild's start and teardown (fence mismatch check). The `spc_rebuild_end_hlc` update ensures subsequent aggregation restarts from epoch 0.

### 13.4 Risk: Fence Cleared Prematurely on Abort (RB-10)

If a rebuild is aborted (e.g., due to admin stop), the fence is cleared during teardown. If partially-migrated data exists on destination targets, a subsequent aggregation run could aggregate data across the (now-cleared) fence boundary before `RB_OP_FAIL_RECLAIM` has a chance to discard the partial migration. This is mitigated by the fail-reclaim being scheduled immediately after abort, but there is a timing window.

---

## 14. Cross-Module Contracts

### 14.1 Container Service

**Scan-time dependency:** `rebuild_container_scan_cb` calls `ds_cont_child_lookup(pool_uuid, cont_uuid)` to obtain a container handle for VOS iteration. If the container is destroyed concurrently (tested by REBUILD6), this returns `-DER_NONEXIST` and the scanner skips it.

**Migrate-time dependency:** The migrate module opens containers by UUID to write received data. Container properties (checksums, compression, encryption) are fetched via IV (`cont_fetch_start_ult`). If a container's properties change during migration (e.g., checksum algorithm change), the migration may produce inconsistent data.

**Contract:** Container service must not finalize container destruction on a target until all rebuild ULTs holding a reference to that container's child handle have released it.

### 14.2 DTX (Distributed Transactions)

**Background — map version vs epoch:** Pool map version (`rt_rebuild_ver`) identifies
which placement layout is active — it determines *where* objects belong. Stable epoch
(`rt_stable_epoch`) is a time-based boundary determining *which* committed data is
eligible for migration — it determines *what* to move. DTX resync ensures all
in-flight distributed transactions touching the current layout are committed or
aborted before the scan starts; stable epoch ensures rebuild only migrates data
committed before rebuild began. See §2.1 for the full epoch-boundary model.

**Pre-scan dependency (map-version gate):** Rebuild waits for
`rt_global_dtx_resync_version >= rt_rebuild_ver` before beginning VOS iteration.
This is a readiness gate: it ensures that all distributed transactions for the
current placement have been resolved on all target engines before scanning begins.
The DTX resync module owns this version counter.

**Orphan cleanup:** `dtx_cleanup_orphan()` is called before the scan. If this function leaves unresolved entries, rebuild may migrate uncommitted data.

**Contract:** DTX resync must complete at or above `rt_rebuild_ver` before signaling the condition variable. Any DTX committed after the stable epoch must not be visible to the rebuild scan.

### 14.3 Checksum

**Migration path:** `srv_obj_migrate.c` includes `daos_srv/srv_csum.h` and handles checksum data throughout:
- `migrate_one.mo_iods_csums` carries per-IOD checksums.
- `migrate_one.mo_csum_iov` carries raw checksum bytes.
- `migrate_fetch_update_inline` fetches data with checksums via `dsc_obj_fetch` with a `csum_iov` parameter.
- If the checksum buffer is too small on first fetch, it reallocates and retries.
- `migrate_csum_calc` recalculates checksums when needed (e.g., for EC reconstruction).

**Contract:** Checksum algorithms and chunk sizes are container properties. If these change between the time an object is scanned and the time it is written at the destination, checksum verification will fail. Rebuild assumes container checksum properties are immutable during migration.

### 14.4 DFS (DAOS File System)

DFS operations (open, create, punch, stat) interact with rebuild at the object layer. Many rebuild tests use DFS (e.g., `rebuild_with_dfs_open_create_punch`, various `rebuild_dfs_*` helpers).

**Contract:** DFS namespace consistency depends on rebuild correctly migrating all object shards. DFS does not distinguish between normal I/O and rebuild-migrated data — it trusts that the object layer provides consistent snapshots.

**Risk:** If rebuild silently drops an object (due to `mpt_fini` returning 0 — RB-02), DFS will see a corrupted namespace after the next failure.

### 14.5 Pool Map Service

Rebuild's identity is fundamentally defined by pool map versions. Any changes to pool map version semantics (e.g., version numbering, component state transitions) directly impact rebuild correctness.

**Contract:** The pool map version used for a rebuild operation (`rgt_rebuild_ver`) must correspond exactly to the pool map that triggered the rebuild. The pool service must not create intermediate map versions without triggering a new rebuild evaluation.

---

## 15. Module Lifecycle and Graceful Shutdown

### 15.1 Module Registration

The rebuild module is registered as `rebuild_module` (`srv.c:3344`) with:
- `.sm_init = init` — initializes `rebuild_gst`, creates mutexes and condition variables.
- `.sm_fini = fini` — destroys mutexes, frees condition variables, tears down IV.
- `.sm_cleanup = rebuild_cleanup` — the graceful shutdown handler.

### 15.2 Graceful Shutdown (`rebuild_cleanup`)

```c
static int
rebuild_cleanup(void)
{
    ds_rebuild_leader_abort_all();
    return 0;
}
```

`ds_rebuild_leader_abort_all()` (`srv.c:2412`):
1. Acquires `rg_lock`.
2. If no rebuild is running, returns immediately.
3. Sets `rg_abort = 1` (signals all queued tasks to be removed).
4. For each RGT on `rg_global_tracker_list`, calls `rgt_leader_abort(rgt)`.
5. Waits on `rg_stop_cond` until all `rebuild_task_ult` instances have exited.
6. Frees `rg_stop_cond`.

**Ordering requirement:** `rebuild_cleanup` is called during engine shutdown, before the pool service is finalized. Target-side rebuild ULTs that are still running will observe `rpt->rt_abort` (set by the leader's abort broadcast) and exit. However, if the network layer is already shut down, the abort broadcast may not be delivered, and target ULTs will rely on their own `rpt->rt_finishing` flag or the engine's ULT-exit check (`dss_ult_exiting`).

**RB-19:** There is no explicit drain of the `rg_queue_list` during cleanup — queued but not-yet-dispatched tasks are simply abandoned. This is safe because they are leader-only in-memory state — they will be regenerated by the next leader via `ds_rebuild_regenerate_task()`.

---

## 16. Public API

The rebuild module exports the following public functions (declared in `src/include/daos_srv/rebuild.h`):

```c
/* Schedule a rebuild operation. Called by the pool service on pool map change. */
int  ds_rebuild_schedule(struct ds_pool *pool, uint32_t map_ver,
                         daos_epoch_t stable_eph, uint32_t layout_ver,
                         struct pool_target_id_list *tgts,
                         daos_rebuild_opc_t rebuild_op,
                         daos_rebuild_opc_t retry_rebuild_op,
                         uint32_t retry_map_ver, bool stop_admin,
                         struct rebuild_task *cur_task,
                         uint64_t delay_sec);

/* Query current rebuild status (called by pool query). */
int  ds_rebuild_query(uuid_t pool_uuid, struct daos_rebuild_status *status);

/* Abort rebuild for a pool/version/generation/term. */
void ds_rebuild_abort(uuid_t pool_uuid, unsigned int ver,
                      unsigned int gen, uint64_t term);

/* Administrator-initiated stop (dmg pool rebuild stop). */
int  ds_rebuild_admin_stop(struct ds_pool *pool, uint32_t force);

/* Administrator-initiated start/resume. */
int  ds_rebuild_admin_start(struct ds_pool *pool);

/* Regenerate rebuild tasks after leader change or manual start. */
int  ds_rebuild_regenerate_task(struct ds_pool *pool, daos_prop_t *prop,
                                uint64_t sys_self_heal, bool auto_recovery,
                                uint64_t delay_sec);

/* Return true if a rebuild is currently running for this pool at this op-code. */
void ds_rebuild_running_query(uuid_t pool_uuid, uint32_t opc,
                              uint32_t *upper_ver, daos_epoch_t *stable_eph,
                              uint32_t *generation);

/* Restart rebuild if rank's work is still in-progress (used for failure recovery). */
void ds_rebuild_restart_if_rank_wip(uuid_t pool_uuid, d_rank_t rank);

/* Abort all rebuilds across all pools (used during engine shutdown). */
void ds_rebuild_leader_abort_all(void);
```

---

## 17. Data Migration Pipeline (`srv_obj_migrate.c`)

### 17.1 Resource Management

Migration throughput is governed by `migr_res_manager`:
- `MIGR_OBJ`: concurrent object ULTs (33% of pool)
- `MIGR_KEY`: concurrent dkey ULTs (67% of pool)
- `MIGR_DATA`: in-flight data bytes

Plus `MIGR_INF_DATA_HULK` for single very-large transfers (> 256 MB).

### 17.2 `mpt_fini` Teardown Behavior

| Behavior | Sites | Status |
|----------|-------|--------|
| Returns error (`-DER_SHUTDOWN` / `mpt_status`) | L708, L739/753, L1914 | Correct |
| Returns `0` silently | L2474, L2481, L2635, L2787, L2835, L2910, L3242 | **Bug (RB-02)** |
| Returns `1` (iterator stop) | L3484 | Correct |
| Void, no status update | L2018 (`migrate_one_ult`) | Bug (RB-02) |

---

## 18. Known Issues

### Issue Catalog

| ID | Severity | Description |
|----|----------|-------------|
| **RB-01** | **Critical** | Abort path sends `pull_done=1` without waiting for `dm_migrating=0`. Leader success branch doesn't check `rgt_abort`. Aborted rebuild treated as successful; reclaim scheduled on incomplete data. |
| **RB-02** | **Critical** | 7 of 10+ `mpt_fini` check sites return `0` instead of error. Migration silently skipped; rebuild reports success with incomplete data. |
| **RB-03** | **Critical** | EC aggregation pause timeout swallowed (`void` return). Scan proceeds with live aggregation. Parity computed over partially-migrated stripe: structurally valid corruption (passes CRC). |
| **RB-04** | **High** | `obj_reclaim_ver` fallback in success branch of `rebuild_task_complete_schedule`. Return value from `ds_pool_tgt_finish_rebuild` not checked. Reclaim scheduled at wrong version boundary. |
| **RB-05** | **Medium** | Leader-switch retry reuses `migrate_pool_tls` with `mpt_fini=1`. Generation not incremented when `version == dst_map_ver`. Poisoned TLS silently skips migration. |
| **RB-06** | **Medium** | IV notification loss (LAZY SYNC). Leader retries every 2s (auto-recovery for transient loss). Prolonged delay only under persistent network failure. |
| **RB-07** | **Medium** | No XS-0 assertion at most RGT mutation sites. `rpt_insert` has it; others don't. |
| **RB-08** | **Medium** | Leader failover regeneration uses current `self_heal` — may differ from original scheduling context. `NO_DATA_SYNC` check blocks all operations, should only block reintegration. |
| **RB-09** | **Medium** | `dst_retry_rebuild_op`, `dst_retry_map_ver`, `rgt_stop_admin` lost on leader failover. New leader regenerates from pool map, not retry context. |
| **RB-10** | **Medium** | Aggregation fence cleared on abort before fail-reclaim runs. VOS aggregation may cross fence boundary before partial data is discarded. |
| **RB-11** | **Medium** | `test_rebuild_wait` uses fixed `sleep(2)` polling. Fragile on slow CI; unnecessarily slow on fast hardware. |
| **RB-12** | **Low** | `rebuild_ults` busy-spins when all queued tasks are deferred. `dss_sleep(0)` yield saturates XS-0. |
| **RB-13** | **Low** | `ds_rebuild_admin_stop` returns `-DER_BUSY` for `RB_OP_RECLAIM` with no explanatory log. |
| **RB-14** | **Low** | `(uint64_t)-1` sentinel used as raw literal (~12 sites). Maintenance risk. |
| **RB-15** | **Low** | `rpt_stale` false-positive window before TLS creation. Negligible — only reachable when `pull_done=1`. Monitoring only. |
| **RB-16** | **Low** | No snapshot-epoch regression tests post-rebuild. |
| **RB-17** | **Low** | No retroactive `self_heal` check on queued tasks. Property change after scheduling has no effect. |
| **RB-18** | **Low** | `NO_DATA_SYNC` reintegration leaves pool degraded with no warning. |
| **RB-19** | **Low** | `rebuild_cleanup` doesn't drain `rg_queue_list` explicitly. Safe (regenerated by next leader). |
| **RB-20** | **Low** | Python ftest fault IDs manually synced with C headers. No automated drift check. |

### Validated Non-Bugs

The following were identified as bugs in earlier document versions but verified as non-exploitable:

| Description | Analysis |
|-------------|----------|
| TOCTOU in `rebuild_global_status_update` (scan_done check before mutation) | Protocol ordering guarantee (§7.5) prevents the consequence. Reordering is an optional 2s optimization, not a data-integrity fix. |
| `riv_status != 0` permanently latches `PULL_DONE` on leader | Same ordering guarantee. `pull_done=1` requires `rt_global_scan_done`, which guarantees `is_rebuild_global_scan_done()=true` on leader. Bypass path unreachable. No wire-format change needed. |

### ID Cross-Reference

For reviewers familiar with earlier versions:

| v4 ID | v1–v3 ID | Description |
|-------|----------|-------------|
| RB-01 | G4 | Abort `pull_done` without draining |
| RB-02 | I4 / C3 | `mpt_fini` returns 0 |
| RB-03 | I10 | EC agg pause timeout swallowed |
| RB-04 | I5 | `obj_reclaim_ver` fallback |
| RB-05 | I3 | Leader-switch TLS reuse |
| RB-06 | G3 | IV notification loss |
| RB-07 | I7 | XS-0 assertions missing |
| RB-08 | N1 | Stale `self_heal` on regeneration |
| RB-09 | N2 | Retry context lost on failover |
| RB-10 | N3 | Fence cleared on abort |
| RB-11 | N7 | Test polling fragility |
| RB-12 | I6 | Busy-spin in dispatcher |
| RB-13 | I8 | Admin stop logging |
| RB-14 | I9 | Sentinel literals |
| RB-15 | I2 | `rpt_stale` false positive |
| RB-16 | I11 | No snapshot tests |
| RB-17 | N4 | No retroactive `self_heal` check |
| RB-18 | N5 | `NO_DATA_SYNC` no warning |
| RB-19 | N6 | Cleanup doesn't drain queue |
| RB-20 | N8 | Ftest fault ID drift |
| *(non-bug)* | I1 | TOCTOU — see §7.5 |
| *(non-bug)* | G5 | IV status latch — see §7.5 |

---

## 19. Module File Organization (Target)

The rebuild implementation should be organized as follows:

| File | Contents |
|------|----------|
| `srv.c` | Module lifecycle: `ds_rebuild_schedule`, `ds_rebuild_query`, `ds_rebuild_admin_stop/start`, `ds_rebuild_regenerate_task`, `rebuild_gst` init/fini. |
| `leader.c` | RGT lifecycle (`rgt_create`, `rgt_destroy`, `rgt_get`, `rgt_put`); leader polling loop (`rebuild_leader_status_check`); IV notification (`rebuild_leader_status_notify`); per-rank status (`rebuild_leader_set_status`, `rebuild_server_get_status`). |
| `target.c` | RPT lifecycle (`rpt_create`, `rpt_destroy`, `rpt_get`, `rpt_put`); target polling loop (`rebuild_tgt_status_check_ult`); TLS management (`rebuild_pool_tls_create/destroy`); `rebuild_tgt_fini`. |
| `scheduler.c` | Task queue: `rebuild_task_create`, `rebuild_task_destroy`, `rebuild_try_merge_tgts`, `rebuild_ults`, `rebuild_task_ult`, `rebuild_task_complete_schedule`, `retry_rebuild_task`. |
| `status.c` | IV reduction: `rebuild_global_status_update`; global completion predicates: `is_rebuild_global_scan_done`, `is_rebuild_global_pull_done`, `is_rebuild_global_done`; slow-engine warnings: `update_and_warn_for_slow_engines`. |
| `scan.c` | Existing file. VOS iteration and object send ULT. |
| `rebuild_iv.c` | Existing file. CaRT IV registration, `rebuild_iv_update`, `rebuild_iv_ent_update`, `rebuild_iv_ent_refresh`. |
| `ras.c` | Existing file. RAS event notification. |
| `rpc.c` / `rpc.h` | Existing files. Wire protocol definitions and registration. |
| `rebuild_internal.h` | Shared type definitions. All `extern` declarations should reference the file that owns each global. |

**External dependency:** `src/object/srv_obj_migrate.c` is part of the object module, not the rebuild module. It is the actual data mover. Any rebuild refactoring that changes the migration interface must coordinate with the object module.

---

## 20. Test Requirements

A correct implementation must pass tests covering the following scenarios:

| Scenario | Test type | Test location |
|---------|-----------|---------------|
| Single target failure, replica pool | Unit + integration | `src/tests/suite/daos_rebuild_simple.c` |
| Single target failure, EC pool (all stripe sizes) | Integration | `src/tests/suite/daos_rebuild_ec.c` |
| Last-rank simultaneous scan_done + pull_done | Unit (IV reduction path) | New test required |
| Target failure during scan phase | Integration | `src/tests/suite/daos_rebuild.c` (REBUILD19/20) |
| Target failure during pull phase | Integration | `src/tests/suite/daos_rebuild.c` |
| Target failure in the 2,000 ms window after scan done | Integration with fault injection | New test required |
| Leader switch during rebuild | Integration | `src/tests/suite/daos_rebuild.c` (REBUILD19: `rebuild_master_change_during_scan`, REBUILD20: `rebuild_master_change_during_rebuild`) |
| Same-version rebuild retry (network error) | Integration with fault injection | New test required |
| Rebuild followed by second failure before reclaim | Integration | `src/tests/suite/daos_rebuild.c` |
| Administrative stop during rebuild | Integration | `src/tests/suite/daos_rebuild_interactive.c` |
| Administrative stop during fail-reclaim | Integration | `src/tests/suite/daos_rebuild_interactive.c` |
| Container destroy during rebuild | Integration | `src/tests/suite/daos_rebuild.c` (REBUILD6: `rebuild_destroy_container`) |
| Rebuild of pool with 100+ snapshots (epoch ordering) | Integration | New test required |
| Multi-pool simultaneous rebuild (inflight limit) | Integration | `src/tests/suite/daos_rebuild.c` |
| Maximum-inflight-RPC rebuild storm | Stress | New test required |
| EC partial-stripe migration | Integration | `src/tests/suite/daos_rebuild_ec.c` |
| Drain single target | Integration | `src/tests/suite/daos_drain_simple.c` |
| Drain single rank | Integration | `src/tests/suite/daos_drain_simple.c` |
| Reintegration with data_sync | Integration | `src/tests/suite/daos_rebuild_simple.c` |
| Reintegration with no_data_sync | Integration | `src/tests/suite/daos_rebuild_simple.c` |
| Incremental reintegration | Integration | `src/tests/suite/daos_rebuild_simple.c` |
| Extend single rank | Integration | `src/tests/suite/daos_extend_simple.c` |
| DFS operations during rebuild | Integration | `src/tests/suite/daos_rebuild_simple.c` (rebuild_dfs_* helpers), ftest (`src/tests/ftest/rebuild/`) |
| Checksum verification post-rebuild | Integration | `src/tests/suite/daos_checksum.c` |
| DTX commit/abort during rebuild | Integration | `src/tests/suite/daos_base_tx.c` |
| Self-heal property change and rebuild gate | Integration | `src/tests/suite/daos_pool.c` |
| Admin start after admin stop | Integration | `src/tests/suite/daos_rebuild_interactive.c` |

### Per-Bug Tests

| Bug | Test |
|-----|------|
| RB-01 | `test_abort_waits_dm_migrating` |
| RB-02 | `test_mpt_fini_propagates_canceled` |
| RB-03 | `test_ec_pause_timeout_aborts_scan` |
| RB-04 | `test_obj_reclaim_ver_on_finish_failure` |
| RB-05 | `test_leader_switch_no_mpt_fini_reuse` |
| RB-06 | `test_global_scan_iv_refetch` |

### 20.1 Python ftest Layer

The Python ftest layer wraps many of the above C tests and adds cluster-level orchestration:

| Test file | Coverage |
|-----------|----------|
| `src/tests/ftest/rebuild/basic.py` | Basic rebuild scenarios |
| `src/tests/ftest/rebuild/with_io.py` | Rebuild with concurrent I/O |
| `src/tests/ftest/rebuild/with_ior.py` | Rebuild with IOR workload |
| `src/tests/ftest/rebuild/cascading_failures.py` | Multiple sequential failures |
| `src/tests/ftest/rebuild/delete_objects.py` | Object deletion during rebuild |
| `src/tests/ftest/rebuild/container_rf.py` | Container redundancy factor |
| `src/tests/ftest/rebuild/container_create_race.py` | Container creation during rebuild |
| `src/tests/ftest/rebuild/pool_destroy_race.py` | Pool destruction during rebuild |
| `src/tests/ftest/rebuild/interactive.py` | Admin stop/start scenarios |
| `src/tests/ftest/rebuild/continues_after_stop.py` | Rebuild continuity after stop |
| `src/tests/ftest/rebuild/widely_striped.py` | Large EC stripe configurations |
| `src/tests/ftest/rebuild/read_array.py` | Read verification after rebuild |
| `src/tests/ftest/rebuild/mdtest.py` | Metadata-intensive rebuild scenarios |
| `src/tests/ftest/rebuild/no_cap.py` | Rebuild without capacity headroom |

**RB-20:** Fault injection synchronization: The fault config utilities (`src/tests/ftest/util/fault_config_utils.py`) define fault IDs that must stay in sync with the C header definitions in `src/include/daos/common.h`. Any refactoring of fault injection points must update both layers.
