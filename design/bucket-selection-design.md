# High-Level Design: Memory Bucket Selection Policy for DAV-V2 (md-on-ssd)

**Author:** Liang Zhen

## 1. Summary

- **Problem**: DAV-V2's single active evictable bucket causes premature exhaustion under concurrent writes, forcing frequent spill-over to non-evictable SOE zones. Rarely-used allocation classes commit full 260 KB chunks for only a handful of allocations, wasting space.
- **Proposal**:
  1. Maintain **N active evictable buckets**; assign new objects via `hash(OID) % N`. N is tiered to md cache size (0 / 1 / 4 / 8 / 16).
  2. Above 75 % bucket usage, allow a **neighbor-class fallback** so a rare class can borrow slots from the next-larger class's active run instead of committing a new chunk.
  3. Keep SOE spill-over as a safety net.
- **Impact**: less spill, better write distribution, less fragmentation. No on-disk format change; downgrade-safe.

---

## 2. Background

### 2.1 Current Architecture

In DAV-V2 (md-on-ssd phase-2), the allocator organizes persistent memory into **zones** (~16 MB each), categorized as:

- **Evictable zones** (`ZONE_EVICTABLE_MB`): Can be evicted from DRAM cache to SSD. Must be **pinned (pre-loaded)** before access.
- **Non-evictable zones** (zone 0, standard NE zones): Always resident in DRAM.
- **SOE zones** (`ZONE_SOE_MB`): Spill-Over-to-Evictable — non-evictable zones used as overflow targets when an evictable bucket is full.

### 2.2 Object-to-Bucket Binding

Each VOS object (`vos_object`) stores a bucket ID (`obj_bkt_ids[0]`) assigned at creation time. All allocations for that object are routed through `vos_obj_alloc()` → `umem_alloc_from_bucket(umm, size, obj->obj_bkt_ids[0])`.

Before any I/O, VOS pins the object's bucket (`obj_pin_bkt` → `vos_cache_pin`), loading it from SSD into DRAM. **One bucket = one SSD page load.** Minimizing buckets per object minimizes pre-load latency.

### 2.3 Current Bucket Selection

- **Assignment**: New objects get the current `active_evictable_mb` via `umem_allot_mb_evictable()`.
- **Switching**: When `active_evictable_mb->space_usage > MB_U75` (75 % full), it is retired and a new one is selected from `emb_qbs` or created.
- **Retired bucket management**: Retired buckets are placed into usage-band queues (`mb_ue`, `mb_u0`, `mb_u30`, `mb_u75`, `mb_u90`). Re-selection prefers partially-filled buckets (30–75 %) over empty ones.
- **Spill-over**: When an object's bucket returns ENOMEM, `palloc_reservation_create()` falls back to `heap_soemb_active_get()`, allocating from a SOE (non-evictable) zone. The object's `obj_bkt_ids[0]` is **not updated**.

### 2.4 Allocation Classes

- VOS registers **16 custom slabs** (32 B–768 B, `HEADER_NONE`) for known metadata structures via `dav_class_register_v2()`.
- DAV-V2 auto-generates ~129 classes (128 B–393 KB) with a **5 % step** for allocations > 768 B that bypass VOS slabs.
- Each class locks a 260 KB chunk to its unit size. Rarely-used classes create sparsely-occupied runs, wasting space.

### 2.5 Problems

1. **Premature bucket exhaustion**: All objects created in a burst share the same bucket. Concurrent writes exhaust it quickly, forcing spill.
2. **SOE pressure**: Frequent spills consume non-evictable (always-resident) memory, reducing available DRAM for other metadata.
3. **Spill has no locality**: `soemb_active_get()` round-robins through SOE zones with no relation to the original object, scattering metadata.
4. **Chunk waste from rare classes**: The 5 % step generates too many allocation classes. Rarely-used classes (especially > 768 B) commit 260 KB chunks for only a handful of allocations.

---

## 3. Goals and Non-Goals

### 3.1 Goals

1. **Reduce spill-over frequency** — Keep most allocations within the object's primary evictable bucket, even for objects that undergo multiple modifications.
2. **Preserve pre-load efficiency** — Each object should ideally need only 1 bucket pinned (one SSD page load).
3. **Distribute write pressure** — Prevent all concurrent writes from competing for a single active bucket.
4. **Reduce ENOSPACE risk** — Minimize wasted space from sparsely-occupied allocation runs within buckets.
5. **Backward compatible** — SOE spill-over remains as a safety net; existing pool formats are preserved.

### 3.2 Non-Goals

- **No on-disk format change.** All new state is runtime-only.
- **No cross-engine coordination** of bucket assignment. Each engine picks locally.
- **No aggregation-driven SOE drain-back** in the initial implementation (tracked as Phase 4, deferred).
- **No change to `obj_bkt_ids[0]` semantics** or the object → primary-bucket binding contract.
- **No change to CaRT/DTX/rebuild paths.** Scope is contained to the DAV-V2 allocator and its VOS caller.

---

## 4. Design

### 4.1 Multiple Active Buckets with Jump Consistent Hash

**Problem**: Single `active_evictable_mb` means all new object assignments compete for one bucket.

**Proposal**: Maintain **N active evictable buckets**, assign objects to `active_buckets[jump_consistent_hash(OID, N)]`. N is determined by md cache size.

**Why jump consistent hash** (Lamping & Veach, 2014):

- **O(log N) time, O(1) memory**, no lookup tables — a good fit for a tight allocator hot path.
- **Minimal reshuffling on N changes**: when N grows from `N` to `N+1`, only `~1/N` of the keys are remapped. This matters for the cache-size tier boundaries — if a pool grows and moves from `N=4` to `N=8`, only about a quarter of new-object assignments land in different slots, preserving locality for the rest.
- **No modulo bias**: `hash % N` skews when the raw hash range is not divisible by N. Jump consistent hash returns a bucket index directly, uniformly distributed for any N.
- **Deterministic**: same OID always maps to the same slot (for a given N), matching Goal 2 (pre-load efficiency).

Reference implementation (from the paper; adapted to C, fits in ~10 lines):

```c
static inline uint32_t
jump_consistent_hash(uint64_t key, int32_t num_buckets)
{
    int64_t b = -1, j = 0;
    while (j < num_buckets) {
        b   = j;
        key = key * 2862933555777941757ULL + 1;
        j   = (int64_t)((b + 1) * ((double)(1LL << 31) / ((key >> 33) + 1)));
    }
    return (uint32_t)b;
}
```

#### 4.1.1 Cache-Size Tiered Policy

The number of active evictable buckets is determined by the md cache capacity:

| MD Cache Size | Active Evictable Buckets | Rationale |
|---------------|--------------------------|-----------|
| ≤ 128 MB     | **0** (all non-evictable) | Only 8 pages total; with 80 % NE, only 1–2 evictable cache slots. Not enough to justify eviction overhead. Make everything NE. |
| ≤ 256 MB     | **1** | 16 pages; ~3 evictable cache slots. Minimal evictable mode — single active bucket like today. |
| ≤ 512 MB     | **4** | 32 pages; ~6 evictable cache slots. Moderate striping. |
| ≤ 1 GB       | **8** | 64 pages; ~13 evictable cache slots. Good distribution. |
| > 1 GB       | **16** | 64+ pages; ample cache. Maximum parallelism. |

**At ≤ 128 MB**: All zones are non-evictable. No eviction, no pinning overhead, no SOE needed. This is effectively a "pure in-memory metadata" mode. The pool's `nemb_pct` is forced to 100 %.

#### 4.1.2 Assignment Logic

Each new object is assigned to a slot via `jump_consistent_hash(oid_key, N)`, where `oid_key = daos_hash_mix96(oid.lo, oid.hi, 0)`. The slot's active bucket is returned as the object's `obj_bkt_ids[0]`.

#### 4.1.3 Per-Slot Switching

Each slot switches independently. When slot `i`'s bucket exceeds `MB_U75` (75 % usage):
1. Retire `avec[i]` back to `emb_qbs` with current usage hint.
2. Pick the next available MB from `emb_qbs` (lowest usage) or create one.

#### 4.1.4 Interface Change

`umem_allot_mb_evictable()` needs to accept an OID hash key so DAV can route to the correct slot via `jump_consistent_hash`:

```c
/* Current */
uint32_t umem_allot_mb_evictable(struct umem_instance *umm, uint32_t flags);

/* Proposed */
uint32_t umem_allot_mb_evictable(struct umem_instance *umm, uint64_t oid_key, uint32_t flags);
```

VOS passes `daos_hash_mix96(oid.lo, oid.hi, 0)` from `obj_allot_bkt()`.

#### 4.1.5 Benefits

- Write pressure distributed across N slots (N depends on cache capacity).
- Objects with same OID always hash to same slot → deterministic bucket locality.
- Independent switching means one hot object doesn't force bucket rotation for all.
- Graceful degradation: tiny caches get pure NE mode (zero overhead); large caches get maximum parallelism.

---

### 4.2 Neighbor-Class Fallback

**Problem**: When an allocation class's run is full within a bucket, the allocator must commit a new 260 KB chunk to that class. For rarely-used classes (especially in the upper size ranges > 768 B), this creates sparsely-occupied runs — most of the chunk is wasted.

**Proposal**: Before committing a new chunk for class X, attempt to allocate from the **next-larger class (X+1)** if it already has an active run with free slots. This fallback is **only activated when the bucket's space usage exceeds 75 % (MB_U75)** — below that threshold, normal chunk creation proceeds as usual since space is plentiful.

#### 4.2.1 Mechanism

When the bucket's `space_usage > MB_U75` and `heap_ensure_run_bucket_filled()` returns ENOMEM for class X:

1. Look up the next-larger class(es) (up to `MAX_CLASS_FALLBACK` steps).
2. If the neighbor class has an active run with free slots, allocate from it.
3. The allocation is recorded as belonging to the neighbor class's run (no special bookkeeping).
4. If no suitable neighbor found, fall through to normal new-chunk creation.

#### 4.2.2 Constraints

- **Only when bucket usage > 75 %** — below this threshold, creating new chunks is acceptable since the bucket has ample space.
- **Only when class X cannot fill from recycler AND has no free chunks** — fallback is a last resort before creating a new run, not the default path.
- **Only when neighbor already has an active run** — don't create a new run for the neighbor class; that defeats the purpose.
- **Limit to 2 steps** — beyond that, waste per allocation exceeds 30 %.
- **Skip for VOS-registered slabs** (≤ 768 B, HEADER_NONE) — these are high-frequency, well-sized. Fallback is most valuable for auto-generated classes > 768 B.
- **Skip for frequently-used classes** — if a class has > 20 allocations in this bucket, it will fill a new run efficiently.

#### 4.2.3 Trade-off Analysis

| Metric | Without Fallback | With Fallback (1 step, 13 %) |
|--------|------------------|-------------------------------|
| Per-allocation waste | 0 | up to 13 % of alloc size |
| Chunk commitment | 260 KB for potentially 3–5 allocs | 0 (uses existing run) |
| Effective waste | ~250 KB (mostly empty run) | ~20 B per alloc × 5 = 100 B |
| Ratio | — | **2600× more space efficient** |

#### 4.2.4 Interaction with Allocation Class Reduction

With 13 % step (60 total classes instead of 129), each step-up wastes at most 13 %. This is a natural pairing:
- Fewer classes → fewer sparsely-used runs overall.
- Neighbor fallback → catches the remaining edge cases where a class has low demand.

---

### 4.3 Spill-Over Behavior (Unchanged, but Less Frequent)

The existing SOE spill-over mechanism remains as a safety net:

```c
/* In palloc_reservation_create() */
if ((mb_id != 0) && (err == ENOMEM)) {
    heap_mbrt_log_alloc_failure(heap, mb_id);
    mb_id = heap_soemb_active_get(heap);  /* fallback to SOE */
    ...
}
```

With multi-bucket striping (§4.1), spill should be less frequent because:
- Write pressure is distributed; no single bucket fills as fast.

With the neighbor-class fallback (§4.2), spill should be less frequent because:
- Rarely-used classes don't consume new chunks unnecessarily.
- More space remains available for frequently-used classes.

---

### 4.4 Combined Effect

| Scenario | 4.1 (Striping) | 4.2 (Neighbor Fallback) | Combined Effect |
|----------|----------------|--------------------------|-----------------|
| Many objects created in burst | Distributed across N buckets | N/A | Each bucket absorbs 1/N of burst |
| Object modified after bucket switch | Still allocates from bound bucket | Activated once bucket > 75 %, avoids new chunk | Modification succeeds without spill |
| Bucket nearly full, rarely-used class needs run | Isolated in one slot | Uses neighbor's existing run | Avoids 260 KB chunk for 3–5 allocs |
| All classes in bucket exhausted | Spill to SOE (unchanged) | Already tried neighbors | SOE is true last resort |
| Cache ≤ 128 MB | N=0, all NE mode | Still applies within NE zones | No eviction overhead at all |

---

## 5. Implementation Phases

### Phase 1: Multi-Bucket Striping (§4.1)
- Add `emb_active_rt` structure and replace `active_evictable_mb`.
- Modify `umem_allot_mb_evictable()` to accept OID hash.
- Update VOS `obj_allot_bkt()` to pass OID hash.
- Update `heap_get_evictable_mb()` to be slot-aware.
- Implement cache-size tiered policy.

### Phase 2: Neighbor-Class Fallback (§4.2)
- Modify `heap_ensure_run_bucket_filled()` to try neighbor classes before creating new chunk.
- Add per-class allocation frequency tracking to decide when fallback is appropriate.
- Add statistics counters for fallback events.

### Phase 3 (Future): Allocation Class Reduction
- Increase step size from 5 % to 13 % (60 total classes).
- Reduces number of sparsely-used runs system-wide.
- Pairs naturally with neighbor-class fallback.

### Phase 4 (Future): SOE Drain-Back
- When aggregation frees space in an object's primary bucket, migrate spilled data back from SOE.
- Reduces long-term SOE pressure.
- Lower priority — only needed if SOE consumption is problematic in practice.

---

## 6. Compatibility & On-Disk Impact

### 6.1 Persistent Layout
- **No on-disk format changes.** All new state (`emb_active_rt`, fallback counters) is runtime-only, recomputed on boot from existing `mb->space_usage`.
- **Object metadata** (`vos_obj_p2_df`): No change. `p2_bkt_ids[0]` continues to store the object's assigned bucket.

### 6.2 Backward Compatibility
- Existing pools continue to work without migration. On boot, all active slots are populated from `emb_qbs` (same initialization path as current `active_evictable_mb`).
- `SOEMB_ACTIVE_CNT` and SOE mechanism unchanged.
- Pools created with the new code can be read by older code (no persistent format change).

### 6.3 Interoperability
- **Mixed-version clusters**: Since bucket assignment is a local engine decision with no cross-node coordination, engines running old code and new code can coexist in the same pool. Each engine manages its own active bucket selection independently.
- **Downgrade**: Safe. The multi-bucket state is runtime-only. An older engine simply reverts to single `active_evictable_mb` behavior on restart.

---

## 7. External Interfaces

### 7.1 Tunable Parameters

| Parameter | Default | Type | Description |
|-----------|---------|------|-------------|
| `EMB_ACTIVE_MAX` | 16 | Compile-time | Maximum active evictable bucket slots |
| `MAX_CLASS_FALLBACK` | 2 | Compile-time | Max neighbor-class steps before creating new chunk |
| `DAOS_EMB_ACTIVE_CNT` | (auto) | Env variable | Override auto-detected active bucket count. Clamped to `[0, EMB_ACTIVE_MAX]`. Overrides the tier table when set. |

### 7.2 Cache-Size Tier Table

Canonical tier table (same as §4.1.1):

| MD Cache Size | `emb_art.cnt` | `nemb_pct` behavior |
|---------------|---------------|---------------------|
| ≤ 128 MB     | 0 | Forced to 100 % (all NE) |
| ≤ 256 MB     | 1 | Normal (default 80 %) |
| ≤ 512 MB     | 4 | Normal |
| ≤ 1 GB       | 8 | Normal |
| > 1 GB       | 16 | Normal |

### 7.3 Observability

Per-engine telemetry counters:
- `bucket_assignments_total` — number of objects assigned to each slot.
- `bucket_rotations_total` — number of slot switches (bucket retirements).
- `neighbor_fallback_total` — number of allocations served by neighbor-class fallback.
- `neighbor_fallback_waste_bytes` — total bytes wasted due to fallback upsizing.
- `soe_spill_total` — SOE spill-over events (existing counter, expected to decrease).

---

## 8. Testing & Validation

> **TODO** — fill in with concrete test names once test scaffolding is drafted.

### 8.1 Unit Tests

- **DAV-V2 allocator tests** (`src/common/tests/dav_v2_ut`):
  - Multi-slot rotation: verify each slot switches independently at MB_U75.
  - Hash distribution: verify `hash(OID) % N` distributes uniformly across slots for representative OID sets.
  - Cache-size tiering: verify `emb_active_cnt_by_cache_size()` returns expected N for boundary sizes (128 MB, 256 MB, 512 MB, 1 GB, > 1 GB).
  - Neighbor-class fallback: verify fallback is not invoked below MB_U75, is invoked above MB_U75, respects the 2-step cap, and skips VOS-registered slabs.
- **VOS object tests** (`src/vos/tests/vos_tests`):
  - Object creation under N=1/4/8/16: verify `obj_bkt_ids[0]` matches `hash(OID) % N`.
  - Pin/unpin behavior unchanged (still 1 bucket per object).

### 8.2 Integration Tests

- **Pool creation across all tiers**: pools with cache sizes at each tier boundary; verify `emb_art.cnt` matches expectation and pool becomes usable.
- **Burst-write workload**: create M objects concurrently; measure SOE spill rate before/after change (expect significant reduction).
- **Downgrade / upgrade**: format pool with new code, mount with old code, and vice versa; verify no data loss and no format errors.

### 8.3 Success Metrics

- `soe_spill_total` reduced by ≥ 50 % on the burst-write workload versus baseline.
- No regression in single-object throughput or latency (within ± 2 %).
- `neighbor_fallback_total` observable but < 5 % of total allocations under steady-state load.
- No new pool format version required.

---

## 9. Risks, Mitigations and Future Works

### 9.1 Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| Hot OID in one slot fills bucket fast | Existing 75 % switch threshold + SOE fallback still available |
| N active buckets = N pinned pages? | No — only the object's own bucket is pinned at I/O time; the N slots are allocator-side state |
| Hash collisions cluster objects | Use high-quality hash (mix96); even 4 slots is enough for typical concurrent RPC count |
| Wasted space in partially-filled retired buckets | `emb_qbs` tracks usage; buckets can be re-activated when a slot needs one |
| ≤ 128 MB forced all-NE limits total pool capacity | Intentional — small caches can't efficiently evict. Users needing more capacity must provision larger cache. |
| Tier boundaries are step-functions | `DAOS_EMB_ACTIVE_CNT` env-var override for advanced tuning |
| Neighbor fallback increases per-allocation waste | Capped at 13 % (1 step) or 30 % (2 steps); only for rarely-used classes; net savings vastly exceed waste |
| Fallback hides real capacity issues | Track fallback frequency; alert if consistently high (indicates class distribution mismatch) |

### 9.2 Future Works

Items deliberately out of scope for this design, tracked for later consideration:

- **Allocation class reduction** — increase step size from 5 % to 13 % (~60 total classes instead of 129). Reduces sparsely-used runs system-wide. Pairs naturally with neighbor-class fallback but can ship independently. (Also listed as Phase 3 in §5.)
- **SOE drain-back** — when aggregation frees space in an object's primary bucket, migrate spilled data back from SOE. Reduces long-term SOE pressure. Lower priority; only needed if SOE consumption is problematic in practice. (Also listed as Phase 4 in §5.)
- **Adaptive N per pool** — currently N is derived from a static tier table. A future revision could adjust N dynamically based on observed workload (bucket rotation frequency, SOE spill rate).
- **Cross-slot rebalancing** — if one slot becomes chronically hotter than others (skewed OID distribution), migrate its bound objects to less-loaded slots. Requires updating `obj_bkt_ids[0]`, which today is treated as immutable.
- **Persistent slot state** — persist `emb_art` to disk to preserve slot bindings across restart. Not needed for correctness (state is reconstructed from `emb_qbs` and `mb->space_usage`), but might reduce warm-up latency after restart.

---

## Appendix A. Data Structures, State Machines and Pseudo Code

### A.1 New Runtime Structures

```c
#define EMB_ACTIVE_MAX 16

struct emb_active_rt {
    struct mbrt *avec[EMB_ACTIVE_MAX];
    unsigned     cnt;  /* actual active count, set by cache-size tier */
};
```

### A.2 Changes to Existing Structures

In `struct heap_rt`, replace:
```c
/* Remove: struct mbrt *active_evictable_mb; */
struct emb_active_rt  emb_art;
```

### A.3 Per-Slot State Machine

Each active slot `avec[i]` follows this lifecycle:

```
                  ┌────────────┐
   init / boot ──►│   EMPTY    │  slot has no bound mbrt
                  └─────┬──────┘
                        │  new object needs slot i
                        ▼
                  ┌────────────┐
                  │   ACTIVE   │  avec[i] = mbrt from emb_qbs (or newly created)
                  │            │  space_usage ≤ MB_U75
                  └─────┬──────┘
                        │  space_usage > MB_U75
                        ▼
                  ┌────────────┐
                  │  RETIRING  │  push avec[i] back to emb_qbs
                  └─────┬──────┘  with usage-band hint
                        │  pick next mbrt (lowest usage) or create
                        ▼
                  ┌────────────┐
                  │   ACTIVE   │  (loops)
                  └────────────┘
```

Transition triggers:
- `EMPTY → ACTIVE`: first `heap_get_evictable_mb_for_obj(oid_key)` where `jump_consistent_hash(oid_key, cnt) == i`.
- `ACTIVE → RETIRING → ACTIVE`: on next assignment when `avec[i]->space_usage > MB_U75`.
- No explicit `ACTIVE → EMPTY` transition — slots stay bound until process restart.

### A.4 Key Functions

```c
/* Determine active bucket count from cache size */
static unsigned
emb_active_cnt_by_cache_size(uint64_t cache_size)
{
    if (cache_size <= (128ULL << 20))
        return 0;  /* all non-evictable */
    if (cache_size <= (256ULL << 20))
        return 1;
    if (cache_size <= (512ULL << 20))
        return 4;
    if (cache_size <= (1ULL << 30))
        return 8;
    return 16;
}

/* Assign bucket to new object */
uint32_t heap_get_evictable_mb_for_obj(struct palloc_heap *heap, uint64_t oid_key)
{
    struct emb_active_rt *art = &heap->rt->emb_art;

    if (art->cnt == 0)
        return 0;  /* all non-evictable mode */

    uint32_t slot = jump_consistent_hash(oid_key, art->cnt);
    struct mbrt *mb = art->avec[slot];

    if (mb == NULL || mb->space_usage > MB_U75)
        mb = rotate_slot(heap, slot);  /* pseudocode: retire old, pick/create new */

    return mb->mb_id;
}
```

### A.5 Neighbor-Class Fallback Logic

```c
#define MAX_CLASS_FALLBACK  2  /* try at most 2 neighbors */

/* Only activate fallback when bucket is above 75 % usage */
if (mb->space_usage <= MB_U75)
    goto create_new_chunk;  /* plenty of space, normal path */

/* In palloc_reservation_create() or heap_ensure_run_bucket_filled() */
for (int step = 1; step <= MAX_CLASS_FALLBACK; step++) {
    uint8_t neighbor_id = class_id + step;
    struct alloc_class *nc = alloc_class_by_id(classes, neighbor_id);

    if (nc == NULL)
        break;
    /* Don't fall back if waste exceeds 30 % */
    if (nc->rdsc.unit_size > size * 130 / 100)
        break;

    struct bucket *nb = mbrt_bucket_acquire(mb, neighbor_id);
    if (bucket_has_active_run_with_slots(nb)) {
        ret = palloc_reservation_create_in_class(heap, nb, nc, size, ...);
        mbrt_bucket_release(nb);
        if (ret == 0)
            return 0;  /* success */
    }
    mbrt_bucket_release(nb);
}
/* Fall through to new chunk creation */
```

**Implementation notes**:
- `mbrt_set_laf()` (locally-allocation-failed) should NOT be set for class X if fallback succeeded — the class isn't truly exhausted, just redirected.
- Track fallback frequency per class via statistics counters to validate effectiveness.

---

## Appendix B. Alternatives Considered

> **TODO** — validate rationale and add rejected alternatives that came up during design review.

### B.1 Per-Object Dedicated Bucket
Each object gets its own bucket at creation. **Rejected**: explodes the number of buckets far beyond cache capacity; every object would trigger its own SSD page load; defeats the "1 pin per object" locality property.

### B.2 Random Slot Selection Instead of Hash
Pick a slot at random on each new-object assignment. **Rejected**: loses determinism — parallel engines or restarts could route the same OID to different slots, complicating debugging and telemetry attribution. Hashing is nearly free and gives stable mapping.

### B.3 LRU-Based Active Bucket Tracking
Track access recency across all evictable buckets and keep the "hottest" N active. **Rejected**: adds bookkeeping on every allocation, provides no clear benefit over the existing `emb_qbs` usage-band queues, and interacts poorly with the 75 % switch threshold.

### B.4 Static N (Not Cache-Size Tiered)
Always use a fixed N (e.g., 8). **Rejected**: small caches (≤ 128 MB, ≤ 256 MB) can't sustain 8 evictable buckets — cache-slot pressure would force constant eviction, defeating the purpose. Tiered N degrades gracefully.

### B.5 Persistent Storage of Active-Slot State
Persist `emb_art` to disk so state survives restart. **Rejected**: current in-memory state is cheaply reconstructed from `emb_qbs` and `mb->space_usage` on boot; persistence would require a format version bump for no measurable benefit.
