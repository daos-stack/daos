# DFuse Read/Write Path Design and Code Breakdown

This document explains how DFuse handles file reads and writes in the current code, including:

- FUSE read path and its fast paths
- FUSE write path and write-back behavior
- Pre-read and chunk-read mechanisms
- Truncation-aware read behavior
- Async event and slab lifecycle
- IO interception library (IOIL) read/write paths
- Locking and coherency model

Files covered:

- `src/client/dfuse/ops/read.c`
- `src/client/dfuse/ops/write.c`
- `src/client/dfuse/ops/open.c`
- `src/client/dfuse/ops/setattr.c`
- `src/client/dfuse/file.c`
- `src/client/dfuse/dfuse.h`
- `src/client/dfuse/il/int_read.c`
- `src/client/dfuse/il/int_write.c`

## 1. Key Runtime Objects

### 1.1 `dfuse_obj_hdl` (open handle state)

Important fields for I/O behavior:

- `doh_linear_read`:
  Per-open-handle mode bit that means "this handle is still eligible for linear-read optimizations".
  It starts enabled for read-friendly opens and is turned off when access pattern diverges (for example,
  non-sequential reads or writes on the same handle). When false, DFuse skips linear-sensitive fast
  paths such as strict pre-read position tracking.
- `doh_linear_read_pos`:
  Next expected file offset for linear progression on this handle. In the strict linear case, each
  successful read advances this value by the number of bytes returned. In the out-of-order aligned
  pre-read case, DFuse updates it to the maximum observed end offset instead of exact sequence order.
  This value is also used by the early-EOF fast path, where a read exactly at this position can be
  answered immediately with 0 bytes once EOF is known.
- `doh_linear_read_eof`:
  Sticky "EOF reached in linear stream" marker for this handle. It is set when a read/pre-read reply
  proves the stream reached end-of-file (for example short read or request at/beyond available data)
  and enables the immediate EOF shortcut for the next probe at `doh_linear_read_pos`. It should be
  cleared/invalidated when writes or non-linear behavior make prior EOF knowledge stale.
- `doh_caching`:
  Indicates if kernel-side caching is enabled for this handle.
- `doh_write_count`:
  Number of writes submitted on this open handle.
- `doh_ie`:
  Points to inode state (`dfuse_inode_entry`).

### 1.2 `dfuse_inode_entry` (inode state)

Important fields for I/O behavior:

- `ie_stat.st_size`:
  Last known file size used for some local decisions.
- `ie_open_write_count`:
  Number of open handles that have issued writes.
- `ie_wlock`:
  RW lock used to coordinate write-back cache flushes.
- `ie_truncated`, `ie_start_off`, `ie_end_off`:
  Tracks special case where file was extended from size 0; reads in untouched ranges are synthesized as zeros.
- `ie_active`:
  Per-active-inode state for read chunking and pre-read.

### 1.3 `active_inode` and pre-read/chunk state

- `active_inode->readahead` (`dfuse_pre_read`): pre-read request state, completion flag, queued pending reads.
- `active_inode->chunks`: list of chunk-read buckets (`read_chunk_data`) used by the 128 KiB aligned read coalescing path.

### 1.4 `dfuse_event` and slabs

Read/write/pre-read requests are represented by `dfuse_event` objects from slab allocators:

- `de_read_slab`
- `de_pre_read_slab`
- `de_write_slab`

Completion callback frees/finalizes the DAOS event and releases slab object.

## 2. Open/Close Decisions That Affect I/O

### 2.1 Open (`ops/open.c`)

`dfuse_cb_open()` determines caching and optional pre-read:

1. Handle setup and DFS `dfs_dup()`.
2. Caching mode derived from container settings, open flags, and timeout config.
3. If data caching is active and file appears not already cached/open, prefetch can be enabled.
4. If parent directory has recent linear-read signal and file size is `0 < size <= DFUSE_MAX_PRE_READ`, pre-read may be enabled.
5. `active_ie_init()` creates/refs `ie_active` and optional `readahead` state.
6. If `O_TRUNC`, DFuse performs explicit `dfs_punch()` and evicts dcache.
7. Reply to FUSE open.
8. If pre-read is selected, `dfuse_pre_read()` submits async read of entire file from offset 0.

### 2.2 Close (`ops/open.c` release)

`dfuse_cb_release()`:

1. Flush outstanding write-back writes via `DFUSE_IE_WFLUSH(ie)`.
2. Updates cache policy depending on whether writes happened (`doh_write_count`) and whether IOIL was used.
3. Decrements `ie_open_write_count` when writes occurred.
4. Drops active inode reference via `active_oh_decref()`.
5. Releases DFS object handle.
6. Updates parent directory linear-read hint based on read behavior.

## 3. FUSE Read Path (`ops/read.c`)

Entry point: `dfuse_cb_read(req, ino, len, position, fi)`.

The path order is important.

### 3.1 Fast path A: linear EOF early return

Trigger conditions (both required):

- `doh_linear_read_eof == true` means this handle already learned EOF from an earlier read result
  (for example a short read in `dfuse_cb_read_complete()`), or from bounded pre-read handling.
- `position == doh_linear_read_pos` means the caller is asking exactly at the next expected offset in
  the same linear stream. This is the common "one more read to confirm EOF" probe pattern.

What DFuse does when triggered:

- Replies immediately with 0 bytes (`DFUSE_REPLY_BUFQ(..., NULL, 0)`).
- Does not allocate a read event for this request.
- Does not call `dfs_read()`.
- Clears linear EOF/linear mode flags for this handle after answering this probe.

### 3.2 Fast path B: pre-read reply

If `active->readahead` exists, `dfuse_readahead_reply()` may satisfy request from pre-read buffer.

Behavior:

1. If pre-read is not complete, request is queued in `readahead->req_list` and handled upon completion.
2. If pre-read completed with error, read fails with that error.
3. If linear-read state no longer matches expected pattern, pre-read is disabled for this request.
  Trigger: `doh_linear_read` is false, or strict linear mode expects `position == doh_linear_read_pos`
  and this request arrives at a different offset.
  Meaning: DFuse no longer trusts the pre-read state to represent the caller's access sequence.
  Action: this function returns false so the caller falls through to later read paths (chunk-read or
  regular `dfs_read()` path). No pre-read buffer reply is sent for this request.
4. Special case: 128 KiB aligned out-of-order requests are allowed and logged.
  Trigger: both `position % 128 KiB == 0` and `len % 128 KiB == 0`.
  Meaning: this is treated as a common aligned access pattern where strict in-order checks are
  relaxed to preserve pre-read usefulness for mixed parallel readers.
  Action: DFuse logs `allowing out-of-order pre read`, updates `doh_linear_read_pos` to the maximum
  observed end offset, and still serves from pre-read if data is available.
5. `readahead_actual_reply()` computes bounded reply length and truncation handling.
  Trigger: pre-read is complete, valid, and selected for servicing the request.
  Meaning: reply length is clamped to available pre-read bytes to prevent reading beyond the
  pre-read buffer.
  Action: one of three outcomes occurs:
  - request starts at or beyond available pre-read length: return 0 bytes (EOF)
  - request partially exceeds available pre-read length: return short/truncated read
  - request fully inside available pre-read length: return full requested length
  In all outcomes, no `dfs_read()` is issued for that request because data comes from pre-read state.

EOF correctness details:

- Reads at or beyond pre-read length return empty reply.
- Reads partially beyond pre-read length return truncated length.
- Linear EOF marker is set when request reaches/passes available pre-read data.

### 3.3 Fast path C: chunk read/coalescing for aligned 128 KiB reads

This path optimizes repeated 128 KiB aligned reads by coalescing up to eight front-end requests into
one 1 MiB back-end DFS read.

Trigger gates (all must pass):

- Request length is exactly 128 KiB.
- Request offset is aligned to 128 KiB boundaries.
- Request is eligible for chunking in current inode state (active chunk state exists and request is
  not in a range the chunk path intentionally excludes, such as end-of-file edge regions where
  short-read handling is preferred).

If any gate fails, the function returns false and read processing falls through to the regular DFS
read path.

Bucket model:

- Chunk size is 1 MiB.
- Each 1 MiB region is a bucket.
- Each bucket is divided into eight fixed 128 KiB slots.
- A request maps to:
  - bucket = floor(position / 1 MiB)
  - slot = (position % 1 MiB) / 128 KiB

Example:

- position 0x00000 to 0x1ffff belongs to bucket 0, slot 0.
- position 0x20000 to 0x3ffff belongs to bucket 0, slot 1.
- position 0x100000 to 0x11ffff belongs to bucket 1, slot 0.

Mechanism details:

1. Lookup existing bucket state
  DFuse scans active bucket list for the computed bucket id.
2. Create bucket state on first hit
  If not found, DFuse allocates read_chunk_data, acquires a read event, and submits one 1 MiB
  async DFS read for the bucket base offset.
3. Register this request as a slot consumer
  The incoming FUSE request handle is stored in the slot entry for that bucket.
4. Coalesce more requests while fetch is in flight
  Later requests for other slots in the same bucket attach to the same bucket state instead of
  issuing additional DFS reads.
5. Complete and fan out replies
  On async completion, chunk callback slices the 1 MiB buffer into 128 KiB slot windows and replies
  each queued slot request from the shared memory.
6. Lifetime and cleanup
  Bucket state tracks entered and exited slot participants; when all participating requests finish,
  bucket state and backing event are released.

Concurrency and ownership notes:

- Bucket list operations are protected by active inode lock.
- Slot accounting is used to avoid freeing bucket state while callbacks are still consuming it.
- On file close, completed buckets are freed immediately; in-flight buckets are marked for deferred
  free in callback path.

Fallback behavior:

- If allocation fails, slot state is inconsistent, DFS submit fails, or shape checks fail, chunk path
  aborts and caller continues with regular per-request DFS read.
- This makes chunk read an opportunistic optimization, not a required correctness path.

### 3.4 Regular DFS read path

If no fast path handled request:

1. Acquire event from `de_read_slab`.
2. Optional mock-zero path for truncated-file extension case.
3. Set event fields and callback (`dfuse_cb_read_complete`).
4. Flush write-back writes with `DFUSE_IE_WFLUSH(ie)` before submitting read.
5. Submit `dfs_read()` with async DAOS event.
6. Signal event thread (`sem_post`) and restock read slab.

Completion callback `dfuse_cb_read_complete()`:

- Replies error if DAOS read failed.
- Updates linear-read state and EOF marker.
- Replies with returned bytes (including short-read truncation semantics).
- Finalizes DAOS event and releases slab object.

### 3.4.1 Mock-zero sub-branch inside regular read (`ie_truncated`)

This path handles sparse/extended-file semantics without a DFS read when DFuse already knows the
requested bytes are logically zero.

When this mode is active:

- `ie_truncated` is true, typically after a size extension from 0 where only parts of the new range
  have been written back.
- `ie_start_off` and `ie_end_off` track the known written subrange seen by DFuse since truncation.

Exact trigger condition in `dfuse_cb_read()`:

1. `ie_truncated` must be set.
2. Request must be fully below known size (`position + len < ie_stat.st_size`).
3. Request must be outside known written range, using either:
   - no known written range yet (`ie_start_off == 0 && ie_end_off == 0`), or
   - request starts at/after end of written range (`position >= ie_end_off`), or
   - request ends at/before start of written range (`position + len <= ie_start_off`).

Interpretation:

- If all three checks pass, DFuse treats the requested interval as definitely unwritten-in-extended
  space and therefore logically zero.
- DFuse synthesizes the reply by zero-filling the read buffer and returns immediately.
- No `dfs_read()` is issued for that request.

Examples:

- Example A (no writes yet after extension):
  `ie_truncated=true`, `ie_start_off=0`, `ie_end_off=0`, request inside file size -> reply zeros.
- Example B (read before written window):
  written window `[64 KiB, 128 KiB)`, request `[0, 32 KiB)` -> reply zeros.
- Example C (read after written window):
  written window `[64 KiB, 128 KiB)`, request `[256 KiB, 320 KiB)` -> reply zeros.
- Example D (overlap written window):
  written window `[64 KiB, 128 KiB)`, request `[96 KiB, 160 KiB)` -> not mock-zero; normal DFS read.

Why this exists:

- It preserves expected sparse/truncate zero semantics.
- It avoids unnecessary backend round trips for ranges DFuse can prove are zero.

## 4. Pre-read Lifecycle Details

### 4.1 Submission (`dfuse_pre_read`)

- Allocates event from `de_pre_read_slab`.
- Sets read length to file size snapshot at open time (`de_readahead_len = ie_stat.st_size`).
  This is not for arbitrary file sizes: pre-read is only selected earlier in open path when
  prefetch/linear-read heuristics pass and `0 < ie_stat.st_size <= DFUSE_MAX_PRE_READ`.
- Submits `dfs_read(..., offset=0)` asynchronously.
- Stores event pointer in `active->readahead->dra_ev`.

Notes:

- For pre-read-eligible files, DFuse attempts to read the entire file as sized at open.
- If file size changes before completion and returned length differs from `de_readahead_len`, DFuse
  discards pre-read data and falls back to regular read paths.

### 4.2 Completion (`dfuse_cb_pre_read_complete`)

- Stores completion rc in `dra_rc`.
- On error: event is finalized/released and `dra_ev` set null.
- If returned length differs from expected pre-read size, event is discarded and pre-read data is invalidated.
- Marks pre-read as complete and drains queued reads via `pre_read_mark_done()`.
- Drops extra active inode reference acquired for pre-read lifetime protection.

### 4.3 Queued reads while pre-read is in-flight

`pre_read_mark_done()` iterates queued `read_req` entries:

- If pre-read failed/unavailable, replies error.
- Otherwise replies from pre-read buffer through `readahead_actual_reply()`.

## 5. FUSE Write Path (`ops/write.c`)

Entry point: `dfuse_cb_write(req, ino, bufv, position, fi)`.

### 5.1 Request validation and setup

1. Compute write length from fuse buffer vector.
2. Zero-length write returns success immediately.
3. Validate position and checked end offset (`position + len`) for overflow.
4. Disable linear-read optimization on this handle (`doh_linear_read = false`).
5. Optionally take read lock on `ie_wlock` when write-back cache is enabled (`dfc_wb_cache`).

### 5.2 Metadata/cache invalidation bookkeeping

Before submission:

- Increment `doh_write_count`.
- If this is first write on handle, increment `ie_open_write_count`.
- If this is first open-writer on inode, evict metadata cache (`dfuse_mcache_evict`).

This ensures stale metadata is not reused across write activity.

### 5.3 Buffer staging and DFS write submission

1. Acquire event from `de_write_slab`.
2. Verify incoming write length fits event iov buffer.
3. Copy fuse data into event buffer via `fuse_buf_copy`.
4. Configure event fields and completion callback (`dfuse_cb_write_complete`).
5. Submit async `dfs_write()`.
6. Update `ie_truncated` write range tracking when active.
7. Update cached `ie_stat.st_size` with checked end position.
8. If write-back cache mode, reply success immediately (before async completion).
9. Signal event thread and restock write slab.

### 5.4 Write completion callback

`dfuse_cb_write_complete()`:

- Non-writeback mode (`de_req != 0`): reply on completion success/error.
- Write-back mode (`de_req == 0`): unlock `ie_wlock` read lock held by writer path.
- Finalize DAOS event and release write slab event.

### 5.5 Error path rollback

On submission/copy/setup failure, path:

- Releases write-back read lock if held.
- Rolls back write counters if they were incremented.
- Replies error to FUSE request.
- Finalizes/releases event object if allocated.

## 6. Truncation and Setattr Interaction (`ops/setattr.c`)

`dfuse_cb_setattr()` sets DFS attrs and updates local truncation tracking:

- On size set from 0 to larger size (with data caching enabled), marks `ie_truncated = true` and resets written range to empty (`start=end=0`).
- Any other size set clears `ie_truncated`.
- This state is used by read/write paths to synthesize zeros and track overwritten range.

State is updated after successful `dfs_osetattr()`.

## 7. Write-Back Flush Synchronization Model

Write-back cache mode means DFuse can acknowledge a write request before backend DFS write
completion, while still preserving consistency at explicit synchronization points.

### 7.1 What write-back mode is in this code

- In write-back mode, `dfuse_cb_write()` submits async `dfs_write()` and replies success to FUSE
  immediately (optimistic completion).
- The async completion callback does not send the write reply in this mode; it primarily performs
  cleanup and lock release.
- In non-write-back mode, reply is deferred until async completion and reflects final backend status.

### 7.2 Requirements to use this mode

- Container/mount config enables write-back behavior (`dfc_wb_cache` path).
- Handle/open mode allows cached I/O path (not forced direct-only behavior).
- File is in regular DFuse FUSE path; IOIL has its own synchronization model.

Operationally, this means the kernel may consider write complete before DAOS commit is observed,
so DFuse must provide an ordering barrier for readers and metadata queries.

### 7.3 Lock protocol used for ordering

Macro: `DFUSE_IE_WFLUSH(ie)` in `dfuse.h`.

- Writer side (wb mode): each submitted write holds inode rwlock in shared/read mode for the
  write lifetime.
- Completion side: async callback releases that shared/read lock when write finishes.
- Flush side (`DFUSE_IE_WFLUSH`): takes inode rwlock in exclusive/write mode and immediately releases.

Why this works:

- Exclusive lock acquisition cannot succeed while any writer still holds shared lock.
- Therefore `DFUSE_IE_WFLUSH` blocks until all in-flight wb writes complete.
- Once acquired and released, caller has a happens-before point against prior wb writes.

### 7.4 Path timeline

1. Write request enters `dfuse_cb_write()`.
2. In wb mode, writer takes shared/read lock and submits async backend write.
3. DFuse replies success to caller immediately.
4. Background completion eventually runs and drops writer shared/read lock.
5. Any path calling `DFUSE_IE_WFLUSH` (for example read/getattr/close paths) waits for step 4 on all
   prior writes before proceeding.

### 7.5 What this guarantees and what it does not

Guaranteed:

- At a `WFLUSH` boundary, no prior wb write remains in-flight.
- Read/getattr/close paths that call `WFLUSH` observe post-write backend visibility ordering.

Not guaranteed:

- Immediate durability/visibility exactly at write reply time in wb mode.
- Per-write synchronous error reporting to caller at reply time (errors can surface after optimistic
  reply depending on path semantics).

### 7.6 Practical implications for this design

- Fast write latency in wb mode comes from decoupling reply from backend completion.
- Correctness is preserved by explicit flush points instead of per-write synchronous completion.
- Bugs in lock acquire/release symmetry here are high impact because they can cause stale reads,
  premature metadata visibility, or deadlocks.

## 8. IOIL Paths (Interception Library)

These bypass FUSE request/reply macros and call DFS directly through intercepted libc operations.

### 8.1 IOIL Read (`il/int_read.c`)

- Builds DAOS sgl from user buffer (`pread`) or iovec (`preadv`).
- Uses DAOS event queue when available, otherwise synchronous DFS call.
- Polls event completion with `daos_event_test` loop.
- Returns bytes read or `-1` with errno-style code.

### 8.2 IOIL Write (`il/int_write.c`)

- Builds DAOS sgl from buffer/iovec.
- Uses async event + poll loop when EQ is available, else sync call.
- Returns bytes written or `-1`.
- Ensures DAOS event is finalized on initialized paths.

## 9. Read/Write Path Selection Summary

### 9.1 Read path order (FUSE)

1. Early EOF from linear-read state.
2. Pre-read reply (if enabled and valid).
3. Chunk-read coalescing for aligned 128 KiB reads.
4. Regular read path: mock-zero sub-branch for untouched truncated ranges, otherwise normal DFS read submission.

### 9.2 Write path modes (FUSE)

- Non write-back mode:
  FUSE reply is sent by async completion callback.
- Write-back mode:
  FUSE reply is sent immediately after submit, and callback only unlocks wb lock and frees resources.

## 10. Common Failure/Corner Cases Worth Understanding

- Reads exactly at EOF and beyond EOF are expected and should return zero bytes.
- Short reads are valid and interpreted as truncation/EOF.
- Pre-read may be abandoned if file size changed before completion.
- Chunk-read path has strict shape constraints and intentionally falls back often.
- In wb-cache mode, visibility ordering relies on `ie_wlock` protocol and explicit flush points.

## 11. Suggested Navigation Order for New Contributors

1. `ops/open.c` (`dfuse_cb_open`, `dfuse_cb_release`)
2. `ops/read.c` (`dfuse_cb_read`, pre-read functions, chunk functions)
3. `ops/write.c` (`dfuse_cb_write`, `dfuse_cb_write_complete`)
4. `ops/setattr.c` (`dfuse_cb_setattr`)
5. `file.c` (`active_ie_init`, `active_oh_decref`)
6. `dfuse.h` (state structs and `DFUSE_IE_WFLUSH`)
7. `il/int_read.c` and `il/int_write.c` (IOIL behavior)

## 12. Glossary

- Pre-read: opportunistic whole-file read on open for small files.
- Chunk-read: 1 MiB backend read serving multiple aligned 128 KiB front-end requests.
- WB cache: kernel write-back cache mode where write replies can be optimistic.
- IOIL: user-space interception path bypassing FUSE syscall path for I/O.
