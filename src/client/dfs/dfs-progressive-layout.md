# DFS Progressive File Layout Design

## Problem Statement

DFS currently stores each regular file as one DAOS array object. The object class is selected when the object ID is generated and is effectively fixed for that file object. This gives predictable behavior, but creates a trade-off:

- Small or medium files may benefit from a compact object class (reduced stripe width, lower metadata overhead, and faster rebuild time).
- Large files may benefit from a wider striped object class for throughput and parallelism.

Large files generally should not use a compact object class because they are constrained by the limited number of storage targets or SSDs that such a class can utilize. In contrast, small or medium files can use a widely striped class even when not all targets in that class are actually used. That creates unnecessary overhead in metadata operations that may need to query or punch all targets, even when many are not populated, because placement is algorithmic and file or object size is not tracked. Furthermore after any engine exclusion, reintegration, or expansion scenarios, self-healing operations on widely striped classes are also computationally more expensive due to layout calculations, which can significantly increase rebuild time for small and medium files without corresponding benefit.

By default, DFS tends to choose classes suitable for large files because DAOS does not know the expected file size at creation time. Most users do not provide explicit hints or a specific object class, so default class selection is often suboptimal for mixed workloads. With a single immutable object class per file object, one class must serve all sizes. The DAOS low-level object API does not currently support an in-place progressive object class change for a combined use case. Changing the object class in place is not supported for an existing object. Therefore, segmenting a logical file across two or more objects preserves immutability while enabling progressive layout.

## Goals

1. Allow a regular file to begin on a compact class and transition to a wider class after a configured  split_off.
2. Start with 2 objects at first, but can extend to more
3. Keep DFS & POSIX visible behavior unchanged for users and applications.
4. Keep the first implementation low risk and incremental.

## Limitations

1. No automatic demotion back to compact class when files shrink.
2. No compatibility mode where old clients mount new layout.
3. Initially, we always query and remove both objects to avoid concurrency issues; this keeps the first implementation simple while still improving rebuild and aggregation performance.

## High-Level Approach

Represent a file as up to two underlying DAOS array objects:

- Head object: compact class (G1, G2, G4, or G32), logical range [0, split_off)
- Tail object: wide class (GX), logical range [split_off, EOF)

split_off is defined once per container and applies to all progressive-layout files in that container. In the future we can consider making that per file like the chunk size if there is a need. Alternatively we can add an option to modify that default per container.

At file creation time, both head and tail object IDs are allocated and persisted in metadata.
Before crossing split_off, effective data placement is head-only.
After crossing, all new logical offsets at or above split_off are mapped to tail.

Logical mapping:

- If off + len <= split_off: access head at off
- If off >= split_off: access tail at off - split_off
- If range crosses split_off: split into head and tail sub-operations

Metadata operations like punch/remove/size query must operate on both objects for correctness. Even if tail bytes were later removed/punched, tail_state remains a one-way transition.

## Head Oclass and split_off Selection Policy (Initial Proposal)

This section defines an initial policy for when progressive layout should be enabled and how to select the head object class and container split_off.

### 1) Disable condition for small pools

Progressive layout is disabled when the pool does not have enough targets to benefit from a non-GX head class.

- If target_nr < 1000, disable progressive layout.
- In this case, use the default DFS file class behavior (GX-style sharding selected by existing DAOS logic).
- For testing only, set `DFS_PL_BYPASS_TARGET_LIMIT=1` to bypass the 1000-target gate while keeping the
  default-selection requirement unchanged.

The 1000-target threshold is an initial value and should be validated with performance and rebuild benchmarks.

### 2) Head EC data/parity selection

Progressive layout is only enabled for the default file-oclass path.

If the user explicitly selects a file object class through container create parameters,
directory settings, or the DFS API, use that class directly and do not apply progressive
layout for that file.

When progressive layout is enabled:

- Select head EC data/parity using the same RF/domain logic as dc_set_oclass().
- Keep this behavior aligned with existing DAOS defaults for array objects.
- Use the default DAOS GX class for the tail object.
- On large systems with many fault domains and targets, EC16P3 is commonly selected by this logic.

Only the redundancy group count (G value) and split_off are introduced as additional policy choices here.

### 3) Head group selection (G value)

Choose a fixed head group count from this set (the predefined group number values for object classes):

- {1, 2, 4, 6, 8, 12, 16, 32}

Inputs:

- grp_size = k + p from selected EC class
- target_nr from pool query
- reserve targets according to RF safeguards

Computation:

1. Compute max_groups_pool = floor((target_nr - reserve_targets) / grp_size).
2. If max_groups_pool < 1, disable progressive layout.
3. Compute a compactness-biased target: g_raw = floor(max_groups_pool / 8).
4. Clamp g_raw to [1, 32].
5. Select g_head as the largest allowed value <= g_raw.

Examples:

1. Example A (medium pool):
  - target_nr = 1200, reserve_targets = 96, grp_size = 9
  - max_groups_pool = floor((1200 - 96) / 9) = floor(1104 / 9) = 122
  - g_raw = floor(122 / 8) = 15
  - clamped g_raw = 15
  - allowed set <= 15 is 12, so g_head = 12

2. Example B (very large pool, EC16P3):
  - target_nr = 9000, reserve_targets = 300, grp_size = 19
  - max_groups_pool = floor((9000 - 300) / 19) = floor(8700 / 19) = 457
  - g_raw = floor(457 / 8) = 57
  - clamped g_raw = 32
  - allowed set <= 32 is 32, so g_head = 32

3. Example C (progressive disabled by this section only):
  - target_nr = 150, reserve_targets = 180, grp_size = 9
  - max_groups_pool = floor((150 - 180) / 9) < 1
  - Result: disable progressive layout

This keeps head layout compact (non-GX) while still scaling with larger pools.

### 4) split_off selection from per-target budget

Define split_off from per-target capacity and the selected head group count.

Inputs:

- min_pool_target_capacity_bytes = minimum per-target capacity assigned to this pool
- head_capacity_fraction = fraction of per-target pool capacity allocated to head data (initial default 0.002)

Input notes:

- head_capacity_fraction controls how much data we allow in head before switching to tail.
- A value of 0.002 means we reserve 0.2% of per-target pool capacity for head data budget.
- This value is intentionally conservative: it keeps head compact for rebuild and metadata efficiency while still providing a meaningful head region.
- Lower values (for example 0.001) switch to tail earlier; higher values (for example 0.005) keep more data in head.

Per-target head budget:

- per_target_budget = head_capacity_fraction * min_pool_target_capacity_bytes

Logical split_off:

- split_off = g_head * k * per_target_budget

Then apply limits and alignment:

- split_off_min = 64 MiB
- split_off_max = 64 GiB
- split_off = clamp(split_off, split_off_min, split_off_max)
- Round split_off up to chunk_size. In this design, chunk_size is already chosen as a valid multiple for EC cell and data-group alignment.

Worked example:

Deployment examples:

- NVMe size per SSD is often 3.84 TiB to 15.36 TiB.
- Engines often use 4-8 SSDs with about 2x as many targets.
- Capacity is frequently split across many pools (for example 10-20), so per-target capacity visible to one pool is much smaller than raw target capacity.

Worked examples:

1. Example A (multi-pool, moderate head group):
  - min_pool_target_capacity_bytes = 200 GiB
  - head_capacity_fraction = 0.002
  - g_head = 12, k = 8
  - chunk_size = 1 MiB
  - per_target_budget = 0.002 * 200 GiB = 0.4 GiB
  - split_off_raw = 12 * 8 * 0.4 GiB = 38.4 GiB
  - split_off_clamped = clamp(38.4 GiB, 64 MiB, 64 GiB) = 38.4 GiB
  - split_off = round_up(split_off_clamped, chunk_size) = round_up(38.4 GiB, 1 MiB) = 38.400390625 GiB

2. Example B (larger pool share, EC16P3 on large system):
  - min_pool_target_capacity_bytes = 400 GiB
  - head_capacity_fraction = 0.002
  - g_head = 32, k = 16
  - per_target_budget = 0.002 * 400 GiB = 0.8 GiB
  - split_off_raw = 32 * 16 * 0.8 GiB = 409.6 GiB
  - split_off = clamp(409.6 GiB, 64 MiB, 64 GiB) = 64 GiB

### 5) Persistence and update timing

- Persist chosen head class and split_off as container defaults at container create/update time.
- Do not recompute split_off per file or per I/O.
- Existing files keep the policy effective at their creation time (unless a future explicit migration feature is added).

## On-Disk Metadata Design

### Existing Inode Entry (base fields)

- mode
- oid (head object id)
- mtime and ctime
- chunk_size (head chunk size)
- oclass (head object class)
- uid and gid
- cached size and object hlc fields

### New Fields for Tail Object

- tail_oid (daos_obj_id_t)
- tail_state (uint8, set to active at creation in current design)

Notes:

- Tail chunk size is the same as head chunk_size and is not stored separately.
- In this design, tail_state is always active and does not gate I/O or metadata behavior.

## DFS Layout Versioning

Introduce DFS layout version 4.

- Containers created with progressive feature support use layout v4.
- Mount should reject unsupported layout versions as today.
- Existing v3 containers remain unaffected.

## In-Memory Structures

Extend dfs object state for regular files:

- head_oh (existing object handle remains primary)
- has_tail (bool)
- tail_oid
- tail_oh

For directories and symlinks, no semantic change.

## Open and Create Behavior

### Create

On create of regular file:

1. Resolve compact and wide classes from policy.
2. Allocate/create both head and tail objects.
3. Write inode with the new tail fields.

On creation, tail_state is set to active.

### Open Existing File

1. Fetch inode entry with both head and tail oid.
2. Build in-memory mapping state (file object handle).

## Tail State

tail_state is initialized as active at file creation and remains active for the lifetime of the file.
It is informational only in this design and does not gate routing, size query, or unlink behavior. In the future, we can explore an efficient way to track the state of the tail(s) to improve metadata efficiency of accessing the tail oid.

## Range Classification and Validation

Both the single-range (read/write) and multi-range (readx/writex) entry points classify the
request against split_off before routing. For progressive-layout files (has_tail) only:

1. Validate every range first: if any range wraps (rg_idx + rg_len < rg_idx) the call returns
   EINVAL before any routing or arithmetic. This guards the idx + len computations used by
   classification and SGL splitting against integer overflow.
2. Classify the set of ranges into one of three dispositions:
   - HEAD: every range lies fully below split_off → route to the head object unchanged.
   - TAIL: every range lies at or above split_off → route to the tail object with each
     rg_idx translated by -split_off.
   - SPLIT: the set contains a mix of head-only and tail-only ranges (or a range that itself
     crosses split_off) → split the request into a head part and a tail part.

Non progressive-layout files (has_tail == false) bypass this entirely and pass straight to the
single underlying array object, preserving existing behavior.

## Read Path Changes

A logical read maps onto one or both array objects per the classification above. The subtlety is
that each array object only knows its OWN size: the head object cannot tell whether the logical
file continues into the tail. This matters for short reads and holes.

### Hole and short-read semantics (POSIX)

A byte-array read fills interior holes (offsets below the object's size that were never written)
with zeros and counts them as read, but for offsets at or beyond the object's size it returns a
short count and leaves the caller's buffer bytes UNTOUCHED (never pre-zeroed). For a
progressive-layout file the logical EOF is split_off + tail_extent, so a region the head object
reports as short can be one of two things:

- An interior hole of the logical file (the tail holds data): it must read back as zeros and be
  counted as read.
- The genuine end of file (the tail is empty): the short count is correct and the buffer past it
  is left untouched per POSIX.

To resolve this without a redundant size query, the array read returns the size it already used to
resolve its own short reads in `daos_array_iod_t.arr_array_size` (the real array size when a short
read was possible, or UINT64_MAX otherwise). DFS reuses that value instead of issuing its own head
size query.

### Head-only read

1. Issue the head read.
2. If it returned the full requested length, finalize immediately (no extra query).
3. If it short-read, query the tail SIZE:
   - tail size == 0: genuine EOF. Keep the short count; leave the buffer past it untouched.
   - tail size > 0: interior hole. Zero each requested range's untouched gap in place (placed
     per range using the head size reported via arr_array_size, since ranges may be unsorted and
     the gap need not be at the physical end of the SGL) and count the full requested length.

The tail size query is only issued on a short read, so the common full-data read pays nothing
extra. The synchronous path does this inline; the asynchronous path expresses it as a two-task
chain (head read → tail get_size, the latter bound to the user event) where the tail get_size body
only runs when the head actually short-read.

### Cross-split (straddle) read

The request is split into a head part and a tail part that are issued concurrently and tracked by a
parent task; the user event (or a private event when blocking) completes only after both finish.
The combined read size is the sum of the two parts' returned bytes, with the same head-hole
correction as above: when the head part short-reads at its own EOF but the tail part reports a
non-zero size (gated on the tail part's arr_array_size, NOT on whether the requested tail subrange
happened to return bytes — a multi-range readx may target a tail range that is itself beyond EOF),
the head region is interior to the logical file, so its untouched gaps are zeroed per range (using
the head part's reported size) and the head's full requested length is counted. Because both array
reads report their sizes, no separate size queries are needed on the straddle path either.

## Write Path Changes

Given logical request [off, off+len):

1. Route lower segment to head, upper segment to tail (split SGL similar to reads). Writes to the
   two array objects are issued concurrently.
2. Wait for all the update operations to complete if the write is blocking. If the call is
   non-blocking, both updates are tracked by a parent task as part of completion.

Writes need no hole or size-query handling: holes are a read-side concern, and a write never
short-completes the way a read does at EOF.

## Truncate and Punch Semantics

Truncate new_size:

- If new_size <= split_off:
  - set head size to new_size
  - set tail size to 0
- If new_size > split_off:
  - set head size to split_off (or keep if already larger only when needed)
  - set tail size to new_size - split_off

Punch range [off, off+len):

- Same split logic as read and write.
- If crossing split, split operation into head and tail portions.

## Unlink and Size Query

Unlink and size query always operate on both head and tail objects. This avoids stale-handle races and keeps behavior consistent with split routing.

## Compatibility and Upgrade

- Existing containers with layout v3 continue unchanged.
- New feature requires layout v4 container.
- Mixed client environments:
  - old clients must not mount v4
  - new clients may support both v3 and v4

## Security and Access Control

No model changes expected. Existing DFS mount mode and uid/gid/mode checks remain in force; they apply equally to head and tail operations.

## MWC tools update

The DFS check tool (`daos fs check`, `dfs_cont_check()`) must account for the tail object in addition
to the head oid stored in the directory entry. A progressive-layout file is backed by two objects
(head and tail), and the object index table (OIT) lists both, so both must be marked as reachable
during the namespace scan; otherwise the tail is treated as a leaked object.

Implemented behavior:

- Namespace marking (`oit_mark_cb`): after looking up an entry, if it is a regular file with an
  active tail (`obj->f.has_tail`), the checker marks `obj->f.tail_oid` in the OIT in addition to the
  head oid. Under `--verify` the tail object is also passed to `daos_obj_verify()`.
- Relink directory descent (`fetch_mark_oids`): when descending a leaked directory (Pass 1 of
  `--relink`), each child entry is read for its tail fields (`tail_oid`/`tail_state`, only present at
  layout >= v4) and any active tail oid is marked too, so it is not double-relinked or removed.
- Consequence for the other modes:
  - `--print`: tail objects of intact files are no longer reported as leaked.
  - `--remove`: tail objects of intact files are no longer punched (previously this destroyed the
    file's `[split_off, EOF)` data).
  - `--relink`: tail objects of intact files are no longer relinked as bogus standalone files.

Orphan (leaked) handling and its limitation:

- When a file is itself leaked (its parent directory entry is lost), both the head and tail become
  unmarked orphans and are relinked into `lost+found` as independent objects. There is no way to
  determine whether an orphaned array was a head or a tail: the head->tail association
  (`tail_oid`/`split_off`/`tail_state`) lived only in the lost parent entry, and both objects are
  plain byte arrays with no back-reference. Each orphan is relinked as a separate, non-PL file — the
  head holding logical bytes `[0, split_off)` and the tail holding `[split_off, EOF)` reindexed from
  0 — leaving the user to stitch the data back together manually if needed.


## Test Plan

Unit and integration coverage should include:

1. Head-only baseline behavior unchanged.
2. Tail activation at boundary:
  - split_off - 1
  - split_off
  - split_off + 1
3. Reads and writes entirely below and above split.
4. Cross-split reads and writes with aligned and unaligned ranges.
5. Truncate to below and above split.
6. Punch ranges below, above, and across split.
7. Rename and unlink for promoted files.
8. Mount compatibility checks between v3 and v4 clients and containers.
9. Hole and short-read handling, sync and async, single-range and multi-range (readx):
  - Head-only read whose range ends in a hole while the tail holds data (interior hole → zeros
    read back and counted) versus an empty tail (genuine EOF → short count, buffer untouched).
  - Straddle readx with unsorted head ranges (an all-hole head range preceding a data head
    range) to verify per-range zero placement rather than a trailing-zero of the SGL.
  - Straddle readx whose tail range is itself beyond EOF while the file still extends into the
    tail elsewhere, verifying the head hole is still zeroed and counted (gate on tail size, not
    on the tail subrange's returned bytes).
  - Range-overflow guard: a wrapping range returns EINVAL.
