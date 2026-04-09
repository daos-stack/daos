# DFS Progressive File Layout Design

## Status

Draft proposal for discussion.

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

## Read Path Changes

Given logical request [off, off+len):

1. If range is fully below split, read head.
3. If fully above split, read tail using translated offset off - split_off.
4. If crossing split, split the read SGL and issue two reads asynchronously, wait for completion (if blocking), and return combined bytes. If the call is non-blocking, we need to track both fetch operations as part of completion.

## Write Path Changes

Given logical request [off, off+len):

1. Route lower segment to head, upper segment to tail (split SGL similar to reads). Writes to the 2 array objects can be done asynchronously.
2. Wait for all the update operations to complete if the write is blocking. If the call is non-blocking, we need to track both updates as part of completion.

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

The DFS check tool needs to be updated to account for tail object scanning and not just the existing oid. Any orphaned objects (head or tail) will be stored in the lost+found. There will be no way to determine if an orphaned object was a head or tail object.

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
