# KV-Cache Offload API and Backend — Index/Array Mapping

> **Storage mapping: index object → one Array per block.** This variant maps each
> block's content-identity key through a shared `daos_kv` **index** object to a
> per-block `daos_array` **data** object. It is one of two documented mappings; the
> alternative — a single shared object with deterministically-addressed,
> self-describing blocks and a single-RPC fetch — is described in
> [KV-Cache Offload — Single-Object Mapping](kvcache_offload_single_object.md).
> The two share the same goals, tiering model, API shape, and I/O engine; they
> differ in the **Data Model** and the sections that follow from it (key encoding,
> atomic publication, reclamation).

## Overview

Large Language Model (LLM) inference engines maintain an attention **KV cache**
(the per-token key/value tensors produced during prefill and reused during
decode). This cache is large, grows with sequence length and batch size, and is
expensive to recompute. Production inference stacks therefore offload KV-cache
blocks out of scarce GPU High-Bandwidth Memory (HBM) into cheaper, larger, and
optionally shared tiers so that they can be reused across decode steps, requests,
and even nodes (prefix reuse).

This document specifies a **framework-agnostic DAOS library** — `libdaoskvc` —
plus Python bindings that allow any KV-cache system (for example vLLM/LMCache,
SGLang HiCache, Mooncake, NVIDIA NIXL/Dynamo) to offload KV-cache blocks onto
DAOS. The store is a **content-addressed, immutable set of blocks**: blocks are
written once, deduplicated, and read back by content-identity key; they are
**never individually deleted during normal operation**, and space is reclaimed
coarsely (bulk purge or a manual, operator-triggered offline GC). The library is
built entirely on existing, well-supported DAOS client primitives (KV objects and
Array objects) and adds the pieces DAOS does not provide today: a KV-cache-
oriented API, content-addressed deduplication, an asynchronous batched I/O
engine, and an optional GPU-direct data path.

The immutable model is a deliberate simplification: because blocks are never
mutated or evicted online, the design needs no per-block reference counts, leases,
pins, lifecycle states, or background garbage collector. Fine-grained online
eviction is recorded as optional future work (see [Roadmap](#phased-roadmap)).

## Goals

- Provide a stable, framework-agnostic C API (and Python binding) for storing and
  retrieving KV-cache blocks keyed by opaque content hashes.
- Deduplicate identical blocks (prefix reuse) so shared prefixes are stored once.
- Keep the store **immutable** with simple reclamation: bulk purge plus a manual,
  operator-triggered offline GC — no online per-block eviction machinery.
- Sustain high throughput for large tensor blobs via batched, asynchronous I/O
  over a host-staging data path.
- Lay groundwork for partial reuse (per layer / per chunk), delivered in a later
  phase.
- Keep a GPU-direct data path open as future work, without committing to
  zero-copy semantics until a DAOS-compatible device-memory path is selected.
- Keep the core data path client-side, requiring **no** DAOS engine/VOS changes.

## Non-Goals

- Managing GPU HBM, block allocation, or attention kernels — the inference
  framework owns Tier 0.
- Deciding *what* or *when* to evict — the framework drives eviction policy; this
  library efficiently moves bytes when instructed.
- **Online / fine-grained per-block eviction** of the DAOS store — reclamation is
  bulk purge plus manual offline GC; automatic recency/size-driven eviction that
  is safe under live concurrent readers is optional future work.
- Relying on DAOS server-side deduplication (`DAOS_PROP_CO_DEDUP`); deduplication
  here is implemented entirely client-side (see [Deduplication](#deduplication)).

## Tiering Model

KV-cache blocks live at different levels depending on how hot they are:

```
  Tier 0: GPU HBM          (live attention cache)      -- owned by framework
     ^  |
     |  v   (framework-driven copies)
  Tier 1: Host DRAM        (transient staging buffers)  -- libdaoskvc buffers
     ^  |
     |  v   (libdaoskvc async I/O)
  Tier 2: DAOS             (capacity, persistent,      -- libdaoskvc backend
                            shared across nodes)
```

- **Tier 0 (GPU HBM)** — where attention reads/writes KV during decode. Owned by
  the framework (for example vLLM's PagedAttention block manager). `libdaoskvc`
  never manages this tier.
- **Tier 1 (host DRAM)** — page-locked staging buffers used to move data between
  the GPU and the network. This is **transient staging, not a residency cache**;
  any host-DRAM caching is owned by the framework/connector, not by `libdaoskvc`.
- **Tier 2 (DAOS)** — persistent, high-capacity, shareable backend where blocks
  ultimately reside.

`libdaoskvc` owns the movement of blocks **between Tier 1 and Tier 2**, and
provides the buffers and hooks the framework uses to move between Tier 0 and
Tier 1.

## Architecture

`libdaoskvc` is composed of the following layers:

1. **Public C API** (`daos_kvcache.h`) — a handle-based, asynchronous-first
   interface for opening a KV-cache namespace and issuing (batched) put / get /
   exists operations, plus offline purge / delete for reclamation.
2. **Backend** (`dc_kvcache.c`) — maps the API onto two kinds of DAOS objects:
   an *index* object (a `daos_kv` mapping each content-identity key to a small
   `{array_oid, size, meta}` record) and *data* Array objects (tensor bytes). The
   store is **immutable** — blocks are written once and never individually deleted
   during operation (see
   [Reclamation](#reclamation-immutable-store-bulk-and-manual-offline-gc)).
3. **Async I/O engine** — a pinned host-buffer pool plus a bounded in-flight
   window that batches and overlaps DAOS reads/writes. This is part of the core
   backend, not a separate component.
4. **GPU-direct data path** (deferred) — host staging via the pinned buffer pool
   is the only committed path. Zero-copy-to-GPU is deferred until a concrete
   DAOS-compatible device-memory path is selected (see
   [GPU-Direct Data Path](#gpu-direct-data-path-deferred)).
5. **Python bindings** — a `DKVCache` class layered on the existing `pydaos`
   extension, exposing batched put/get and DLPack tensor hand-off (host tensors;
   see [Python Bindings](#python-bindings)).
6. **Connector adapters** (later) — thin shims mapping each framework's block/key
   model onto `daos_kvcache`.

```
        Inference framework (vLLM / SGLang / Mooncake / NIXL)
                              |
             connector adapter (thin, per-framework)
                              |
         +--------------------+--------------------+
         |  Python: DKVCache (pydaos extension)    |
         +--------------------+--------------------+
                              |
         +--------------------+--------------------+
         |  C API: daos_kvcache.h                  |
         |  Backend: dc_kvcache.c                  |
         |   - index object (daos_kv)              |
         |   - data objects (daos_array)           |
         |   - async I/O engine (pinned pool)      |
         |   - GPU-direct (deferred; host stage)   |
         +--------------------+--------------------+
                              |
                       libdaos (client)
                              |
                        DAOS engines
```

## Data Model

A **KV-cache namespace** is a logical collection of blocks that lives in its own
DAOS container. Within that container, each block is materialized by two DAOS
objects: an entry in a shared **index** object (a `daos_kv`) that maps the
block's content-identity key to a small record, and a **data** object (a
`daos_array`) that holds the block's bytes. The sections below cover the
container and its layout type, the index object, the data objects, and how a
namespace is bootstrapped.

### Container and layout type

A kvcache store lives in its own DAOS container with a dedicated layout type,
`DAOS_PROP_CO_LAYOUT_KVCACHE`. Creating one is a typed operation —
`daos_kvc_cont_create` — that sets the layout type and schema properties and
initializes the registry root object in a single step, so the container is ready
to use with no lazy first-open setup. On open, the library checks the layout type
and rejects anything that is not a kvcache container, so it neither misreads nor
clobbers other containers (and the type keeps those tools from touching
ours). The registration touch points for the new type are listed in
[Build Integration](#build-integration).

### Index object

The index is a single **`daos_kv` object** per namespace, mapping each block's
content-identity key (a dkey) to a small record:

| field       | Contents                                                     |
|-------------|--------------------------------------------------------------|
| `array_oid` | Object ID of the Array holding the block's bytes.            |
| `size`      | Total block size in bytes.                                    |
| `meta`      | Canonical identity descriptor validated on every hit (see [Key identity](#key-identity-and-validation)). |
| `created`   | Creation timestamp (advisory; used only by age-based offline GC). |

Because the store is **immutable** — a block is written once and never mutated or
individually deleted during operation — the index needs no lifecycle state, no
reference counts, no holder/pin/lease records, and no per-block generations. A
flat `daos_kv` (one value per key) therefore suffices. This is the core
simplification of the design: the entire distributed-eviction machinery that a
mutable, fine-grained-reclaimed shared store would need is absent by
construction. Enumerating the keys (`daos_kv_list`) lists every block in the
namespace, which is all a **manual offline GC** needs to purge selectively (see
[Reclamation](#reclamation-immutable-store-bulk-and-manual-offline-gc)).

#### Key encoding

The `daos_kv` API derives the dkey length with `strlen` (see
[dc_kv.c](../../src/client/kv/dc_kv.c)), so a raw hash with embedded NUL bytes is
not a valid key. The library encodes each content-identity key as a
**fixed-length, NUL-free string** (lowercase hexadecimal, or base32 for
compactness). The public API accepts raw bytes and performs this encoding
internally; callers never pass binary keys.

The key covers **full block identity**, not just a token prefix, and is derived
from both the content hash and a canonical metadata digest (see
[Key identity](#key-identity-and-validation)), so two blocks with different
semantics can never share a key. Identical identity produces an identical key,
which is the basis for deduplication.

### Data objects

Block bytes are stored in DAOS **Array** objects, **one Array per block**. Because
blocks are immutable and never individually mutated, there are no generations: a
key maps to exactly one Array for the block's lifetime, and that Array is freed
only in bulk or by manual offline GC (see
[Reclamation](#reclamation-immutable-store-bulk-and-manual-offline-gc)). A single
`daos_array_write()`/`daos_array_read()` targets one Array handle, so per-block
objects are read and written **in parallel** but not combined into a single
larger transfer.

Creating one object per block does not scale for free: large deployments may
create millions of objects, each incurring object-ID allocation, metadata
creation, open/close, and eventual destroy. The design bounds this cost (see
[Object management and cost](#object-management-and-cost)).

A **packed-segment layout** (many blocks co-located as byte ranges in a shared
Array) is a possible future optimization but is **not an interchangeable
drop-in**: because `daos_array_destroy()` frees a whole Array, packed mode needs
its own offset/length metadata, tombstones for freed ranges, live-range
accounting, background compaction, and atomic relocation of surviving blocks. It
is therefore a separate research track (informed by the P0-proto object-cost
data), not the P1 default. P1 commits to one Array per block.

Partial (per-layer / per-chunk) access is **not** part of the P1 data model. When
introduced in P4, it is expressed through a versioned range descriptor mapping
onto the Array `daos_array_iod_t`, subject to that API's descriptor limits for
small ranges (`DAOS_ARRAY_LIST_IO_LIMIT` / `DAOS_ARRAY_RG_LEN_THD`, see
[daos_array.h](../../src/include/daos_array.h)).

### Example: a block and its two objects

Take **Llama-3-8B** (32 layers, 8 KV heads, head-dim 128, bf16) with a 16-token
block. One block holds the K and V tensors for those 16 tokens across all layers
— `2 (K+V) × 32 × 8 × 128 × 2 bytes` per token × 16 tokens ≈ **2 MiB**.

Suppose block #0 covers the prompt prefix `"You are a helpful assistant."`. The
key is computed from that prefix plus the model/layout metadata — **not** from the
2 MiB of tensors (see [Key identity](#key-identity-and-validation)):

```
meta = { model: llama-3-8b@<rev>, dtype: bf16, layers: 32,
         kv_heads: 8, head_dim: 128, layout: paged-v1, block: 16 tok }
key  = hex( combine( prefix_hash(tokens[0..15]), digest(meta) ) )   →  "a3f19c…e7"
```

That one block maps onto the two DAOS objects like this:

```
key "a3f19c…e7"
   │   (index lookup)
   ▼
 daos_kv   —  dkey "a3f19c…e7" → { array_oid: 0x4a2f:0x0001,
                                    size:      2097152,
                                    meta:      {llama-3-8b, bf16, …},
                                    created:   … }
   │   (array_oid)
   ▼
 daos_array 0x4a2f:0x0001  —  the 2 MiB of bf16 K/V bytes (immutable)
```

- **put:** compute `key`; it is absent, so write the 2 MiB into a new Array
  `0x4a2f:0x0001` and `COND_DKEY_INSERT` the index row.
- **another request with the same prefix:** computes the **same** `key` → hit →
  writes nothing, reads Array `0x4a2f:0x0001` (cross-request/cross-node reuse).
- **get:** look up `key` in the `daos_kv`, validate `meta`, read the Array.

The next block (tokens 16–31) is a **different** `daos_array` with its own
`daos_kv` row, keyed by a hash that chains in the prefix through token 31 — so
blocks chain and reuse stops exactly where two sequences diverge.

### Namespace bootstrap

Because a kvcache container is created with a typed operation (see
[Container and layout type](#container-and-layout-type)), the **registry root
object is created at container-create time** at a fixed, layout-defined OID —
just as DFS creates its superblock. There is no lazy first-open initialization,
no per-application OID to supply, and no collision risk: the container is
dedicated to the kvcache layout, so the library owns every object in it. The API
takes an **options structure** for schema, a typed **create** entry point, and an
**open** entry point:

```c
typedef struct {
        daos_oclass_id_t object_class;   /* Array object class             */
        daos_size_t      chunk_size;     /* Array chunk size               */
        uint32_t         format_version; /* on-disk/serialization version  */
        uint32_t         hash_algo;      /* meta digest hash algorithm     */
        uint32_t         flags;          /* create-if-absent, read-only, … */
} daos_kvc_open_opts_t;

/* Create a dedicated kvcache container (layout type DAOS_PROP_CO_LAYOUT_KVCACHE)
 * and initialize its registry root object. Analogous to dfs_cont_create. */
int daos_kvc_cont_create(daos_handle_t poh, uuid_t cuuid /* out */,
                         const daos_kvc_open_opts_t *opts, daos_event_t *ev);
/* Open a namespace within a kvcache container; validates the container's layout
 * type is KVCACHE and that its schema matches opts. */
int daos_kvc_open(daos_handle_t coh, const char *name,
                  const daos_kvc_open_opts_t *opts, daos_handle_t *kvch,
                  daos_event_t *ev);
```

- **Container create.** `daos_kvc_cont_create` creates the container with
  `layout_type = KVCACHE` and the schema properties, then creates the registry
  root object at its fixed OID. It is safe to re-run: the layout type plus a
  conditional root-object create make repeated or concurrent creation converge on
  one container with one root and no leak.
- **Namespace registration.** Within the registry root, a namespace name maps to
  a record `{ index_oid, object_class, chunk_size, format_version, hash_algo }`.
  A namespace is created on first open by allocating its index OID and inserting
  the record with **`DAOS_COND_DKEY_INSERT`**; the single winner's entry is
  authoritative, and a loser discards its speculatively allocated OID and re-reads
  the winning entry. Concurrent first-opens are therefore safe without a
  distributed lock.
- **Open.** `daos_kvc_open` first **validates the container's layout type is
  `KVCACHE`** (else `-DER_MISMATCH`), then looks up (or conditionally creates) the
  named namespace and checks its object class, chunk size, and format/hash version
  against `opts`. The registry record is the single source of truth for a
  namespace's schema, so all clients agree on layout and version.

## Key identity and validation

**The key is derived from the block's *inputs*, not from hashing its tensor
bytes.** The content hash is computed over the **token prefix** — a chained,
rolling hash of the token IDs, roughly `hash(prev_block_hash ‖ this_block's
tokens)` — combined with the identity metadata below. This matters for two
reasons. First, a `get` must be able to check whether a block is already cached
**before** spending a GPU to compute it, so the key has to be derivable from the
request's inputs, not from KV tensors the caller has not produced yet. Second, it
is cheap: a `put` folds in a handful of token IDs, never a checksum of the
multi-megabyte payload. The KV tensors are a deterministic function of these
inputs, so keying on the inputs is equivalent to keying on the tensors but
computable up front. (Frameworks such as vLLM/SGLang already maintain these
prefix hashes for their GPU-side cache, so the connector reuses them.)

A content hash keyed only on the token prefix is unsafe: two blocks with the same
tokens but different tensor semantics would collide and one would silently serve
wrong data. The key must therefore cover the **full identity** that makes two
blocks byte-for-byte interchangeable, at minimum:

- model identifier and version / weights revision;
- tensor shape and element `dtype`;
- KV layout and a layout **version**;
- layer and chunk coordinates;
- any adapter/context that changes the tensor (for example LoRA/adapter id,
  quantization scheme, attention/rope configuration);
- the token prefix itself.

The library stores the canonical identity as the `meta` descriptor in the index
record and **validates it on every hit**: on `get`, the stored `meta` is compared
against the caller's expected `meta`. A mismatch is treated as a stable,
non-retryable error — reported as `-DER_MISMATCH` — rather than returning the
bytes, which turns an accidental collision (or a stale layout version) into a
safe failure instead of silent corruption. Callers must handle `-DER_MISMATCH`
explicitly; it is not resolved by retrying `put`, since a retry cannot replace an
existing key's value in place.

To make genuine collisions astronomically unlikely in the first place, the
storage **dkey is derived from both the content hash and a canonical digest of
`meta`** (not the content hash alone). Two blocks that differ in any identity
field therefore land on different dkeys and cannot collide; the on-hit `meta`
comparison remains as defense-in-depth against a residual hash collision.

#### Canonical serialization

Because identity must be identical across languages, processes, and library
versions, the digest is **never** computed over an in-memory C struct (which
carries padding, native endianness, pointers, and drifts as fields evolve).
Instead the design fixes, before the API is frozen:

- a **versioned canonical serialization** of `meta` — an explicit, tag-length
  ordered encoding (CBOR-style) with fixed little-endian integers, canonical
  field order, no padding, and a leading `format_version`, so the byte string is
  reproducible everywhere;
- a **named hash algorithm** (for example BLAKE3 or SHA-256) applied to that
  serialization to produce the `meta` digest and the combined identity key.

Both the serialization version and the hash algorithm are recorded in the
namespace registry so that a version or algorithm change is a detectable
incompatibility rather than silent divergence. Callers supply the identity
fields; the library performs the canonical serialization and hashing so all
clients agree byte-for-byte.

## Object management and cost

One Array object per block is simple but, at scale, object-management cost can
dominate. The design bounds this cost with an explicit policy rather than
allocating and destroying objects ad hoc:

- **Range OID allocation.** Object IDs are drawn in bulk via
  `daos_cont_alloc_oids()` (see [daos_cont.h](../../src/include/daos_cont.h)) and
  handed out locally, avoiding a metadata round-trip per block.
- **Handle caching.** Open Array handles are pooled and reused across operations
  on the same block instead of open/close per I/O.
- **Object-class and chunk-size policy.** A namespace fixes the Array object
  class (redundancy/striping) and chunk size to match typical block sizes,
  chosen at `daos_kvc_open` time rather than per block.
- **Batched destroy on reclamation.** Array frees happen only during bulk purge
  or manual offline GC (see
  [Reclamation](#reclamation-immutable-store-bulk-and-manual-offline-gc)),
  batched rather than issued per block during operation.

The **packed-segment layout** — co-locating many blocks as byte ranges within a
shared Array — is **not** a drop-in for this per-block model and is **not** the
P1 default: because `daos_array_destroy()` frees a whole Array, packed mode needs
its own tombstones, live-range accounting, compaction, and atomic relocation (see
[Data objects](#data-objects)). It is a separate research track informed by the
P0-proto object-cost data, not a layout P1 selects. P1 commits to one Array per
block.

Deduplication is implemented **entirely client-side** and does not depend on the
DAOS server-side dedup container property, which is not currently relied upon.

Deduplication reuses the content-derived key: because the key is derived from the
block's full identity, identical blocks produced by different requests or nodes
map to the same key and are stored exactly once. A **put** whose key already
exists writes no data (dedup hit); a **get** returns the existing bytes after
validating the stored `meta` (see [Key identity](#key-identity-and-validation)).

### Atomic publication

Even in an immutable store, a `put` must not let a reader observe a half-written
block. Publication is therefore **data-first with a single conditional commit**:

1. Derive the block's content-identity key (content hash + metadata digest).
2. **Dedup check.** If the key already exists in the index, this is a hit: return
   immediately, writing no Array. (If the stored `meta` does not match, return
   `-DER_MISMATCH`; see [Key identity](#key-identity-and-validation).)
3. **Write data (miss only).** Allocate an Array OID and write **all** block bytes
   into the new immutable Array. Nothing references it yet, so no reader can
   observe it.
4. **Commit.** Insert the index record `{array_oid, size, meta, created}` with
   **`DAOS_COND_DKEY_INSERT`**. This single conditional insert is the commit
   point: readers only ever see a key that points at a fully written, immutable
   Array. If the insert loses a concurrent race (another writer published the
   same key first), the just-written Array is a harmless **orphan** — the winning
   copy is byte-identical — and it is reclaimed by GC (below).

**Crash orphans.** A writer that dies between steps 3 and 4 leaves an Array with
no index entry. Because the store is immutable and content-addressed, such an
orphan is never referenced and never harmful; it is reclaimed by **bulk purge**
(which frees everything) or, for selective offline GC, via an optional
**pending-write list** — a small `daos_kv` recording in-flight `array_oid`s,
cleared on commit — that makes orphaned Arrays enumerable. There are no lifecycle
states, generations, leases, or pins to leave inconsistent, and no multi-object
transaction is required: publication is one Array write plus one conditional
index insert.

### Reclamation (immutable store: bulk and manual offline GC)

The store is **immutable**: a published block is never mutated and is **not
individually deleted during normal operation**. This removes the entire class of
delete-versus-read races a mutable shared store faces — there are no pins,
holders, leases, reference counts, lifecycle states, generations, resurrection,
or a background per-block garbage collector, and **reads never write metadata**.
Space is reclaimed **coarsely and explicitly**:

- **Bulk purge (primary).** Reclaim capacity by destroying the whole namespace's
  data — its index object, its Array objects, and any pending list — and, if
  desired, recreating it empty. This is a small number of DAOS object destroys,
  frees everything at once (including crash orphans), and is the default way to
  bound capacity.
- **Manual offline GC (operator-triggered).** A maintenance operation invoked
  when deletion is safe. It can **purge all** (equivalent to bulk purge) or
  **delete a selected set** — an explicit list of keys, or all keys older than a
  cutoff using the advisory `created` timestamp, discovered by enumerating the
  index (`daos_kv_list`). For each selected key it removes the index entry and
  destroys the backing Array, and it sweeps orphaned Arrays via the optional
  pending-write list.

**Safety contract.** Offline GC can be made **self-enforcing** rather than a soft
operator promise: the maintenance path opens the container in **exclusive mode**
(`DAOS_COO_EX`, see [daos_cont.h](../../src/include/daos_cont.h)), which succeeds
only if **no other client has the container open** (else it fails with
`-DER_BUSY`) and blocks new opens while held. Running purge/GC under an exclusive
handle therefore *guarantees* the store is quiesced — no reader can race a
delete — turning "the operator guarantees safety" into a checked precondition.
(`DAOS_COO_EX` requires the caller to be the container owner.)

Where a caller deliberately runs GC **without** exclusive mode (to reclaim while
traffic continues), the blast radius is deliberately small:

- KV-cache blocks are **regenerable**, so deleting a block that is still wanted
  causes a **cache miss → recompute**, never data loss or corruption.
- The only genuine hazard is destroying an Array a client is **mid-read** from;
  that read fails with an I/O error the caller treats as a miss and
  retries/recomputes. Because Arrays are immutable and content-addressed, a
  re-`put` reproduces byte-identical data — there is no torn or silently corrupt
  result.

So the recommended path is **exclusive-mode offline GC during a maintenance
window** (fully race-free by construction); non-exclusive GC concurrent with live
traffic remains safe at the cost of occasional retriable read misses on exactly
the blocks being deleted.

**Capacity is an operational responsibility.** With only bulk and manual
reclamation, capacity is bounded by how often the operator purges/GCs, monitored
against pool/container usage. Automatic, **fine-grained per-block eviction**
(recency/size-driven and safe under live concurrent readers) is deliberately
**out of scope** and recorded as optional future work (see
[Roadmap](#phased-roadmap)); it is exactly the distributed pin/lease/lifecycle
machinery this immutable design avoids, and would be reintroduced only if a
workload genuinely needs online eviction without quiescence.

`exists()` is **advisory only** — a hint for admission decisions, not a lock. In
normal operation nothing deletes blocks, so a hit is stable; during an offline GC
a block may disappear between `exists()` and `get()`, which the caller handles as
a miss.

## API Surface (C)

The public header `daos_kvcache.h` follows existing DAOS conventions
(handle-based, `daos_event_t` for async, blocking when `ev == NULL`). Indicative
surface — exact signatures are finalized during P1:

```c
/* Create a dedicated kvcache container (layout type DAOS_PROP_CO_LAYOUT_KVCACHE)
 * and initialize its registry root object; analogous to dfs_cont_create. */
int daos_kvc_cont_create(daos_handle_t poh, uuid_t cuuid /* out */,
                         const daos_kvc_open_opts_t *opts, daos_event_t *ev);
/* Open/close a KV-cache namespace. Open validates the container layout type is
 * KVCACHE and the schema in opts; see Namespace bootstrap. `name` (not
 * `namespace`, a C++ keyword) identifies the namespace within the container. */
int daos_kvc_open(daos_handle_t coh, const char *name,
                  const daos_kvc_open_opts_t *opts, daos_handle_t *kvch,
                  daos_event_t *ev);
int daos_kvc_close(daos_handle_t kvch, daos_event_t *ev);

/* A key is the raw content-hash bytes plus the caller's identity metadata; the
 * library hex/base32-encodes the hash into a NUL-free index dkey and stores/
 * validates meta on every hit. */
typedef struct {
        const void          *hash;      /* raw content-hash bytes            */
        size_t               hash_len;
        const daos_kvc_meta_t *meta;    /* model/shape/dtype/layout identity */
} daos_kvc_key_t;

/* Whole-block operations (P1). Immutable store: put writes once (dedup no-op if
 * the key already exists); get reads. No holder/pin arguments — reads never take
 * a lease and never mutate metadata. Buffers in sgl are owned by the caller and
 * must remain valid until the operation completes (ev fires, or the call returns
 * in blocking mode). */
int daos_kvc_put(daos_handle_t kvch, const daos_kvc_key_t *key,
                 const d_sg_list_t *sgl, uint64_t flags, daos_event_t *ev);
int daos_kvc_get(daos_handle_t kvch, const daos_kvc_key_t *key,
                 d_sg_list_t *sgl, size_t *size, daos_event_t *ev);
/* Advisory presence hint; stable in normal operation (nothing deletes), but not
 * a guarantee against a concurrent offline GC. */
int daos_kvc_exists(daos_handle_t kvch, const daos_kvc_key_t *key,
                    bool *exists, daos_event_t *ev);

/* Manual, offline reclamation (maintenance) — run when deletion is safe (see
 * Reclamation). For a race-free purge, open the maintenance handle on a
 * container opened DAOS_COO_EX (exclusive), which fails with -DER_BUSY if any
 * other client has the container open. purge destroys the whole namespace's
 * data; delete removes a specific set of keys and their Arrays; gc_orphans
 * sweeps crash-orphaned Arrays via the pending-write list. */
int daos_kvc_purge(daos_handle_t kvch, daos_event_t *ev);
int daos_kvc_delete(daos_handle_t kvch, uint32_t n, const daos_kvc_key_t *keys,
                    int *item_rcs, daos_event_t *ev);
int daos_kvc_gc_orphans(daos_handle_t kvch, daos_event_t *ev);

/* Pinned-buffer management. To hit the pinned/registered fast path, callers
 * obtain buffers from the library (or register their own) and pass those in the
 * sgl. Arbitrary, non-registered buffers are still accepted but incur an
 * internal bounce copy into the pinned pool (see Async I/O Engine). */
int daos_kvc_buf_acquire(daos_handle_t kvch, size_t size, void **buf);
int daos_kvc_buf_release(daos_handle_t kvch, void *buf);
int daos_kvc_buf_register(daos_handle_t kvch, void *buf, size_t size);
int daos_kvc_buf_unregister(daos_handle_t kvch, void *buf);

/* Batched (vector) variants: issued in parallel through the async I/O engine.
 * One event signals overall completion, but every item carries its own status
 * in item_rcs[i] so callers can distinguish committed from failed items on
 * partial failure, timeout, or cancellation. Puts are idempotent (content-
 * addressed conditional insert), so a retried item never double-writes. */
int daos_kvc_put_batch(daos_handle_t kvch, uint32_t n,
                       const daos_kvc_key_t *keys, const d_sg_list_t *sgls,
                       uint64_t flags, int *item_rcs, daos_event_t *ev);
int daos_kvc_get_batch(daos_handle_t kvch, uint32_t n,
                       const daos_kvc_key_t *keys, d_sg_list_t *sgls,
                       size_t *sizes, int *item_rcs, daos_event_t *ev);
```

P1 operations are **whole-block**: `sgl` describes the entire block. Partial
(per-layer / per-chunk) access is deferred to P4, which adds ranged variants
carrying a versioned range descriptor (the `layout` field is reserved for this
and unused in P1). This keeps the P1 surface and its verification consistent —
no partial-reuse behavior is promised before the descriptor exists.

## Async I/O Engine

Throughput depends on where the caller's buffers come from. The library keeps a
pool of reusable host buffers, but the term "pinned" must be precise — three
distinct kinds of registration exist and the portable core only provides the
first:

- **OS-locked memory** (`mlock`/`madvise`): keeps pages resident to avoid
  page-fault stalls during DMA. This is what the committed host-staging core
  provides; it does **not** by itself make memory CUDA- or transport-registered.
- **CUDA host registration** (`cudaHostRegister`): required for fast GPU↔host
  copies. It is only relevant to the deferred GPU path and is **not** implied by
  `mlock`.
- **Transport registration**: DAOS/Mercury registers bulk buffers per transfer
  inside the RPC path; DAOS exposes **no client-side buffer-registration cache**
  API today, so the library cannot pre-register buffers with the transport and
  must not claim to.

The library can hand its own OS-locked buffers straight to a DAOS bulk transfer
(`daos_kvc_buf_acquire` / `daos_kvc_buf_register`); an arbitrary caller SGL is
copied into one first:

- **Registered/acquired buffers** take the fast path: no bounce copy.
- **Arbitrary buffers** incur one **internal bounce copy** into a pooled buffer
  before/after the transfer, so peak-throughput claims apply to the
  registered/acquired path.

### Resource limits and admission

Bounding in-flight operations alone does not bound memory. The engine also caps,
per namespace handle: **queued (not-yet-started) batches**, **total items and
bytes in flight**, and **pinned bytes held by the pool**. When a limit would be
exceeded, submission is refused with **`-DER_AGAIN`** (the caller retries with
backpressure) rather than growing unboundedly, and `daos_kvc_buf_acquire` either
blocks or returns `-DER_AGAIN` according to a documented flag. Scheduling across
competing submitters is **fair** (FIFO within a priority) so a large batch cannot
starve small ones. Concrete limit defaults are set and tuned in P0-proto.

On that basis the engine:

- Reuses pooled buffers rather than reallocating per operation.
- Bounds the number of **in-flight operations** (mirroring the existing `pydaos`
  bulk model, which keeps up to 16 operations in flight) to overlap network and
  storage latency without unbounded memory use.
- Issues many per-block Array writes/reads **in parallel** (parallel batching).
  Because each `daos_array_write()` targets a single Array handle, per-block
  writes are not merged into one larger transfer; true coalescing into fewer,
  larger writes is only possible under the packed-segment layout alternative and
  is out of scope for the default per-block model.
- Signals completion via `daos_event_t` / callbacks, with per-item status for
  batches (see [Failure and idempotency](#failure-and-idempotency-semantics)),
  so a connector knows exactly which blocks are durable (write) or resident
  (read), enabling overlap with compute.

The offload (evict) and prefetch (load) paths are non-blocking: the framework
copies GPU to a pinned buffer and hands it off, and the library completes the
DAOS transfer asynchronously.

## Failure and idempotency semantics

Because operations are asynchronous and batched, the API defines explicit
failure and idempotency behavior so a caller can always recover a consistent
view after a timeout, cancellation, or partial failure:

- **Per-item status.** Batch calls take an `item_rcs[]` array; every item's
  outcome is reported independently. The single `daos_event_t` reports overall
  completion, but success/failure is read per item, so a partially failed batch
  never leaves the caller guessing which puts committed.
- **Idempotent puts.** A put is content-addressed and committed by a single
  conditional insert, so retrying after a timeout is safe: it either finds the
  key already present (no-op) or completes the insert. There are no reference
  counts to double-increment.
- **Commit is the conditional index insert.** A put is durable once its
  `DAOS_COND_DKEY_INSERT` commits; a get for the same key observes the block only
  after that, never a partial write. A caller that times out can re-issue the put
  (a dedup no-op if the first attempt actually committed).
- **Cancellation is best-effort; completion is mandatory.** `daos_event_abort()`
  only *tries* to abort and is currently effectively a no-op
  ([daos_event.h](../../src/include/daos_event.h)); the caller must still wait or
  poll for the event to actually complete. Timeout or abort therefore does **not**
  by itself establish any item's outcome: `item_rcs[]` and all caller buffers
  remain live and owned by the caller until the event completes, at which point
  the per-item statuses are authoritative.
- **Buffer ownership.** Caller-provided `sgl` buffers must remain valid until the
  operation completes. On get, `size` reports the actual block size; if the
  supplied buffer is too small the call returns `-DER_REC2BIG` with the required
  size and no partial copy is assumed.
- **Idempotency token.** The content-hash key itself serves as the idempotency
  token for put; there are no holder/lease operations to coordinate.

### API lifetimes and edge semantics

To avoid use-after-free and ambiguous ownership, the header fixes these rules:

- **Argument lifetimes.** For an async call, every referenced input — `key`
  (including its `hash` and `meta`), the `sgls`/`keys` arrays, and the output
  arrays `sizes` and `item_rcs` — must remain valid and unmodified until the
  event completes. Only fixed-size scalars are copied at call time; arrays and
  buffers are borrowed, not copied.
- **`daos_kvc_close` with operations in flight.** Close is refused with
  `-DER_BUSY` while operations or acquired buffers remain outstanding on the
  handle; the caller must drain (wait on events, release buffers) first. Close
  never cancels or frees in-flight work underneath the caller.
- **Buffer release/unregister while in use.** Releasing or unregistering a buffer
  that is still referenced by an in-flight operation is a caller error, reported
  as `-DER_BUSY`; the buffer must be idle. Released buffers return to the pool;
  unregistered caller buffers are handed back to the caller.
- **Pool exhaustion.** Whether `daos_kvc_buf_acquire` blocks or returns
  `-DER_AGAIN` on an exhausted pool is selected by a documented flag, consistent
  with the admission policy above.

## GPU-Direct Data Path (Deferred)

GPU-direct is **deferred**: this design commits to **no** zero-copy-to-GPU
behavior until a concrete, DAOS-compatible device-memory path has been selected
and validated. The only committed data path is **host staging** via the pinned
buffer pool (GPU↔pinned host↔DAOS), which is fully portable and requires no
transport or backend changes.

The reason for deferral is that DAOS has no GPUDirect Storage / cuFile / CUDA
integration today, and the obvious options each have an unresolved gap rather
than a ready integration point:

- **cuFile cannot operate on DAOS Array (object) handles.** cuFile's API is
  defined over a registered file descriptor/path, whereas the backend exposes
  DAOS object handles — so "cuFile over Arrays" has no integration point.
- **Native transport path via CART/Mercury `FI_HMEM`.** Landing Array bulk
  transfers directly in GPU memory would require registering device memory as
  bulk buffers in the shared transport; `crt_hg_bulk_create()` registers host
  pointers only today. This is unimplemented transport work, not an available
  capability.
- **File-based path via DFS/dfuse + cuFile.** Usable only if blocks are exposed
  as files through a separate DFS-backed backend, with its own POSIX-layer and
  cuFile-driver/compatibility-mode requirements.

Until one of these is chosen and proven against DAOS, the document makes no
performance claim beyond the host-staging path. Selecting and prototyping a
device-memory path is a prerequisite for any future GPU-direct phase (see
[Roadmap](#phased-roadmap)).

## Python Bindings

Most KV-cache frameworks are written in Python, so the SDK exposes a `DKVCache`
class through the existing `pydaos` extension (`pydaos_core.py` +
`pydaos_shim.c`):

- Batched `put` / `get` mapping onto the C batch API.
- **DLPack** tensor hand-off that avoids the *Python-binding* copy for host
  (CPU) tensors. It is **not** GPU-zero-copy: while host staging is the only
  backend, a GPU-resident tensor still incurs a GPU-to-host transfer before it
  can be stored (and host-to-GPU on retrieval). DLPack removes the extra Python
  copy, not the device transfer.
- Semantics consistent with the existing `DDict` (dict-like) ergonomics where it
  makes sense.

## Connector Adapters (Later)

Each framework integration is a thin adapter (on the order of a few hundred
lines) that maps the framework's block/key model onto `daos_kvcache`:

- **vLLM / LMCache** — implement the LMCache `StorageBackend` interface.
- **SGLang HiCache** — implement the HiCache storage backend hooks.
- **Mooncake** — provide a Mooncake Store backend.
- **NVIDIA NIXL / Dynamo** — provide a NIXL plugin.

The framework-agnostic core is validated first; connectors follow.

## Server-Side Impact

The core design is **client-side only** on the data path. It composes a `daos_kv`
index object, Array objects, conditional inserts (`DAOS_COND_DKEY_INSERT`), key
enumeration (`daos_kv_list`), and object destroy — all with full existing
server-side support, and no distributed transactions, leases, background
services, or engine/VOS changes. The one cross-component piece is registering the
`KVCACHE` container layout type, a control-plane/tooling change with no
storage-engine impact (see [Build Integration](#build-integration)).

GPU-direct is deferred and makes no server-side commitment yet. If a future
phase selects the native device-memory path, it would require device-memory bulk
registration (`FI_HMEM`) in the shared CART/Mercury transport
(`crt_hg_bulk_create()` registers host pointers only today); the DFS/cuFile
alternative would instead layer on the existing POSIX path. Neither is scheduled
until a concrete path is selected, and the committed host-staging path needs no
transport or server change.

## Build Integration

- New public header `src/include/daos_kvcache.h`, registered in
  `src/include/SConscript`.
- New module directory `src/client/kvcache/` containing `dc_kvcache.c`,
  `kvcache_internal.h`, a `SConscript`, and a `README.md`, following the existing
  `array/` and `kv/` module pattern.
- `src/client/SConscript` gains `SConscript("kvcache/SConscript")` alongside the
  existing `array`, `kv`, and `api` modules.
- Python bindings extend `src/client/pydaos/` (`pydaos_core.py`,
  `pydaos_shim.c`).
- **First-class container type** `DAOS_PROP_CO_LAYOUT_KVCACHE`: add the enum value
  in `src/include/daos_prop.h`; add its string mapping in `daos_parse_ctype` /
  `daos_unparse_ctype` in `src/include/daos/common.h`; the range check in
  `src/common/prop.c` accepts it once rebuilt; the Go control plane
  (`src/control/lib/daos/`) and `daos` CLI surface it via `daos_unparse_ctype`.
- Any optional GPU-direct dependency is wired through `site_scons/components` and
  guarded in the `kvcache` `SConscript`, matching how other optional
  prerequisites are handled. The default host-staging path has no extra
  dependency.
## Phased Roadmap

- **P0 — Design/spec** (this document) plus an API header sketch.
- **P0-proto — De-risking prototype (spike, gates P1).** A small prototype that
  validates the mechanisms most likely to be wrong before full implementation:
  - **Immutable publication + dedup** — data-first Array write then
    `DAOS_COND_DKEY_INSERT` commit; confirm no reader observes a half-written
    block, a dedup hit writes no Array, and a lost publish race leaves only a
    harmless byte-identical orphan.
  - **Crash recovery** — kill a writer between the Array write and the index
    insert and confirm the orphan Array is never referenced and is reclaimed by
    bulk purge (and by the optional pending-write sweep). No poisoned or dangling
    entries.
  - **Offline GC correctness** — validate bulk purge (frees index + Arrays +
    orphans), selective delete (by key list and by `created` age via
    `daos_kv_list`), and both safety modes: **exclusive-mode** GC (`DAOS_COO_EX`
    open fails with `-DER_BUSY` if anyone else holds the container, so purge is
    race-free), and non-exclusive GC where a mid-read delete yields a retriable
    read error (never corruption) and a re-`put` reproduces byte-identical data.
  - **Object-count scaling** — measure one-Array-per-block cost at target block
    counts (OID allocation, open/close, destroy); compare against the packed
    layout as a separate research track. Fixes the storage/object policy for P1.
  - **Typed container + bootstrap** — `daos_kvc_cont_create` sets the container
    layout type to `KVCACHE` and creates the registry root at its fixed OID; open
    rejects a non-`KVCACHE` container; concurrent namespace first-open uses an
    idempotent conditional insert with loser cleanup and schema/version checks.
  The prototype is not shipped.
- **P1 — Core C backend**: first-class `KVCACHE` container type with typed
  `daos_kvc_cont_create` (creates the container and its registry root); `daos_kv`
  index (content-identity key → `{array_oid, size, meta, created}`) + one Array
  per block, data-first publication committed by a single conditional insert,
  client-side deduplication with hit-time identity validation over a versioned
  canonical `meta` serialization, the host-staging I/O engine (parallel batching,
  resource-limited admission), and reclamation via bulk purge + manual offline GC
  (`daos_kvc_purge` / `daos_kvc_delete` / `daos_kvc_gc_orphans`).
- **P2 — Python bindings**: `DKVCache` class and DLPack tensor hand-off.
- **P3 — GPU-direct (deferred)**: host staging is the only committed path.
  Zero-copy-to-GPU is gated on first selecting and prototyping a concrete
  DAOS-compatible device-memory path (`FI_HMEM` transport support or a
  DFS/cuFile backend); no zero-copy work is scheduled until that selection is
  made.
- **P4 — Partial reuse**: versioned range descriptor and ranged put/get for
  per-layer/per-chunk access (not part of the P1 surface or verification).
- **P5 — Connectors**: vLLM/LMCache first, then SGLang, Mooncake, and NIXL.
- **Future work (unscheduled)** — fine-grained, **online per-block eviction** of
  the DAOS store (recency/size-driven, safe under live concurrent readers). This
  reintroduces the distributed reference/lease/lifecycle machinery the immutable
  design deliberately omits, and would be explored only if a workload needs
  online eviction without quiescence.
- **Future work (unscheduled)** — explore an optional node-local shared cache
  (for example POSIX shared memory) in front of DAOS; out of scope for this
  design.

Phases P2, P3, and P4 can proceed in parallel once P1 lands. P1 does not start
until P0-proto validates immutable publication, crash recovery, offline GC, and
object scaling. Connectors (P5) depend on the Python bindings (P2).

## Verification

- **API review** — the header compiles standalone and the surface is reviewed
  against the vLLM/LMCache `StorageBackend` and Mooncake Store interfaces to
  confirm it can be mapped without gaps.
- **Performance** — microbenchmarks measure throughput (GB/s) and operation rate
  against a `pydaos` `DDict`/`DArray` baseline for representative block sizes, and
  compare one-Array-per-block object cost against the packed layout at scale.
- **Correctness** — tests cover: deduplication (a put whose key exists writes no
  Array; identical content across nodes stores one copy); hit-time identity
  validation (collision/stale-layout returns `-DER_MISMATCH`, never silent
  corruption); data-first publication (no reader observes a half-written block; a
  lost publish race and a crash-orphan Array are both harmless and reclaimable);
  cross-language identity (the versioned canonical `meta` serialization + named
  hash produce byte-identical keys across languages/versions); and typed container
  creation + namespace first-open (`daos_kvc_cont_create` sets layout type
  `KVCACHE` and the registry root; open rejects a non-`KVCACHE` container with
  `-DER_MISMATCH`; concurrent first-open uses an idempotent conditional insert
  with loser cleanup and schema/version compatibility).
- **Reclamation** — bulk purge frees the index, all Arrays, and orphans; selective
  offline GC removes exactly the chosen keys and their Arrays (and sweeps
  orphans); **exclusive-mode** (`DAOS_COO_EX`) GC refuses to run while any other
  client holds the container (`-DER_BUSY`), and in non-exclusive mode the mid-read
  contract holds — destroying an Array under a concurrent reader yields a
  retriable error, not corruption, and a re-`put` reproduces byte-identical data.
- **Failure semantics** — batch partial-failure and timeout tests confirm per-item
  status is accurate only after event completion (abort is a no-op), buffers stay
  live until completion, and retried puts never double-write.
- **Resource limits and edge semantics** — submission past the configured
  batch/item/byte/pinned-byte limits is refused with `-DER_AGAIN` (not unbounded
  growth) with fair scheduling, and `close` with work in flight and buffer
  release/unregister while in use return `-DER_BUSY`.
- **Buffer path** — registered/acquired-buffer transfers avoid a bounce copy;
  arbitrary-buffer transfers are verified correct through the internal bounce
  copy, and the throughput gap between the two is measured.
- **GPU-direct** — no parity target until a device-memory path is selected; once
  one is, it is validated for data parity against the host-staging path.

## Open Considerations

1. **Key ownership and identity** — caller-supplied content hashes (recommended,
   keeps hashing policy in the framework) versus library-computed hashing
   (pluggable hasher); either way the hash must cover full identity (model,
   shape, dtype, layout version, layer/chunk, adapter context) and is validated
   against stored `meta` on every hit.
2. **Storage layout** — one Array per block is the committed P1 layout; a packed
   segment layout is a **separate research track** (needs tombstones, live-range
   accounting, compaction, atomic relocation — not a drop-in), evaluated only
   with the P0-proto object-cost data.
3. **Reclamation model** — bulk purge + manual offline GC only; capacity is an
   **operational responsibility** (monitor usage and purge/GC). Fine-grained,
   online per-block eviction is optional future work and would reintroduce the
   reference/lease/lifecycle machinery this design omits.
4. **Offline GC concurrency** — the recommended, race-free mode opens the
   container **exclusively** (`DAOS_COO_EX`), guaranteeing no concurrent readers
   during purge/GC; running GC non-exclusively alongside live traffic is also
   supported, trading that guarantee for occasional retriable read misses on the
   blocks being deleted (no correctness impact, since blocks are regenerable and
   immutable).
5. **Buffer contract** — how far to push registered/acquired buffers on callers
   (fast path) versus accepting arbitrary buffers with a bounce copy, given
   framework buffer-ownership constraints.
6. **GPU-direct dependency** — deferred; no zero-copy-to-GPU path is committed
   until one is selected and prototyped against DAOS: `FI_HMEM` transport support
   versus a DFS/cuFile backend. Host staging is the only committed path.
