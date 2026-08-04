# KV-Cache Offload API and Backend

> **Storage model: deterministically-addressed, self-describing blocks in a small
> set of shared DAOS objects.** A block lives as a dkey whose location is a pure
> function of its content-identity key, with its metadata co-located alongside its
> bytes — so a `get` is a **single fetch RPC** with no index indirection, and the
> same read that returns the data also validates it. This is what lets the backend
> match the fetch latency of a hand-written native plugin while keeping full
> identity validation and clean per-block deletion, and it is the foundation for
> **access-driven, server-side reclamation** (see [Reclamation](#reclamation)) —
> the capability that most distinguishes a DAOS-native KV-cache backend from a
> POSIX or generic object store.

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
written once, deduplicated, and read back by content-identity key. Because blocks
are regenerable, reclamation can be aggressive: capacity is bounded today by bulk
purge and a manual offline GC, and the target design is an **access-driven,
server-side sweep** that evicts what is no longer being read without any client
enumeration (see [Reclamation](#reclamation)). The library is
built entirely on existing, well-supported DAOS client object primitives
(`daos_obj` fetch/update/punch) and adds the pieces DAOS does not provide today: a
KV-cache-oriented API, content-addressed deduplication, an asynchronous batched
I/O engine, and an optional GPU-direct data path.

The immutable model is a deliberate simplification: because blocks are never
mutated, the design needs no per-block reference counts, leases, pins, or
lifecycle states, and **reads never write metadata**. That last property is
preserved even by the access-driven reclamation above, which tracks recency in
volatile server memory rather than by updating stored state.

## Goals

- Provide a stable, framework-agnostic C API (and Python binding) for storing and
  retrieving KV-cache blocks keyed by opaque content hashes.
- Deduplicate identical blocks (prefix reuse) so shared prefixes are stored once.
- Reclaim capacity by **access**, not by insertion age, without ever writing on
  the read path or enumerating keys from the client.
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
- Deciding *what* or *when* to evict **from GPU memory** — the framework drives its
  own eviction policy; this library efficiently moves bytes when instructed.
  Reclaiming the DAOS tier is this design's concern (see
  [Reclamation](#reclamation)).
- **Insert-time expiry** as a reclamation policy — a block's age says nothing about
  its value, so a shared prefix read by every request must not be discarded merely
  for having been written early.
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
2. **Backend** (`dc_kvcache.c`) — maps the API onto a small fixed set of shared
   DAOS objects. A block's location is a pure function of its content-identity
   key (key → object + dkey), and under that dkey the block's bytes (`"d"` akey)
   and identity metadata (`"m"` akey) sit together, so one fetch returns both.
   There is **no index object** and no per-block object allocation. The store is
   **immutable** — blocks are written once and never mutated; they are reclaimed
   only as whole blocks (see [Reclamation](#reclamation)).
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
         |   - k shared block objects (daos_obj)   |
         |   - key -> (object, dkey) addressing    |
         |   - co-located meta + data akeys        |
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
DAOS container. Within that container, blocks are **not** given one object each
and are **not** routed through a separate index. Instead a small fixed set of
shared DAOS objects holds every block, and a block's location is a **pure
function of its content-identity key**: the key selects one of the shared objects
and names a dkey within it. Under that dkey the block is **self-describing** — its
bytes and its identity metadata sit side by side as two akeys — so a single fetch
returns both. The sections below cover the container and its layout type, how a
key is addressed, the per-block object schema, and how a namespace is
bootstrapped.

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

### Deterministic addressing

There is no index object. Each block is addressed by deriving its location
directly from the content-identity key `K`:

```
object = O[ hash_hi(K) mod k ]     # one of k shared objects in the namespace
dkey   = base32( hash(K) )         # deterministic, fixed-length, NUL-free
```

- **The k shared objects** are created at namespace bootstrap (their OIDs
  recorded in the registry) and striped/replicated per the namespace object
  class. `k` is a small fan-out (tens–hundreds) chosen for parallelism and to
  spread dkey-hash load across targets; it is **not** per-block, so there is no
  per-block object allocation, open/close, or destroy — the object-count cost
  that a one-object-per-block layout must bound simply does not arise.
- **dkey hashing** (DAOS `MULTI_HASHED`) distributes dkeys across the object's
  targets, so distribution and parallelism come from the key hash — exactly how a
  native single-object plugin gets them — but without giving up metadata or clean
  deletion (below).

**Addressing parameters are immutable for the life of a namespace.** `k`, the
hash algorithm, the encoding, and `format_version` are *inputs to the address*:
changing any of them relocates every block, so a namespace that changed them
would silently miss on all existing keys. They are therefore fixed at namespace
create, recorded in the registry, and **validated on every open** (mismatch is
`-DER_MISMATCH`, not a silent re-hash). Changing them means creating a new
namespace and letting the old one age out — which is cheap, because the store is a
regenerable cache.

Because the address is a pure function of `K`, any client or connector (LMCache,
NIXL, Mooncake) computes it **locally** and issues one I/O: there is no index
fetch, no OID-allocation service, and no metadata lookup on the hot path. The
registry is read **once, at `daos_kvc_open`**, which caches
`{block_oids[k], k, hash_algo, format_version}` in the handle and opens the `k`
object handles; every subsequent operation is local computation plus a single RPC
(see [Fetch latency and plugin integration](#fetch-latency-and-plugin-integration)).

#### Key encoding

The object dkey must be a NUL-free byte string (DAOS key routines take an
explicit length, and the encoded form must round-trip cleanly), so the library
encodes `hash(K)` as a **fixed-length, NUL-free string** (base32, or lowercase
hexadecimal). The public API accepts raw bytes and performs this encoding
internally; callers never pass binary keys.

The key covers **full block identity**, not just a token prefix, and is derived
from both the content hash and a canonical metadata digest (see
[Key identity](#key-identity-and-validation)), so two blocks with different
semantics can never share a dkey. Identical identity produces an identical dkey,
which is the basis for deduplication.

### Block object schema

Under its dkey, a block is stored as **two akeys** in the shared object:

| akey  | Type                        | Contents                                     |
|-------|-----------------------------|----------------------------------------------|
| `"m"` | `IOD_SINGLE`                | Canonical **metadata header**: `format_version`, `hash_algo`, the **full-width identity hash**, the canonical `meta` (model, dtype, layout and layout version, layer/chunk coords, adapter context), the exact `size` in bytes, and `created` (advisory, for age-based GC). Validated on every hit (see [Key identity](#key-identity-and-validation)). |
| `"d"` | `IOD_ARRAY` (record size 1) | The block **bytes**, as a byte extent `recx [0, size)`. Ranged extents make per-layer/per-chunk access (P4) a sub-range read. |

Both akeys live under the **same dkey**, so a single `daos_obj_fetch` with two
iods (`"m"` single + `"d"` extent) returns the metadata **and** the bytes in one
RPC, and a single `daos_obj_update` writes both together. That update is one
operation on one dkey: the dkey determines placement, so both akeys land in the
same target group and are committed at a single epoch under one DTX — there is no
window in which `"d"` exists without its `"m"`. Because the store is
**immutable**, a published dkey is written once and never mutated; there are no
generations, lifecycle states, reference counts, or holder/lease records. The
metadata header travelling next to the bytes is exactly what lets the store be
both single-RPC-fast **and** self-validating — the two properties a
key-string-only layout is forced to trade against each other.

**Block size is bounded, and a block is never split across dkeys.** A namespace
fixes a maximum block size; a `put` above it is rejected with `-DER_REC2BIG`
rather than being sharded over multiple dkeys. This is deliberate: multi-dkey
blocks are exactly what forces a native plugin to track a shard count per key
(and to leak or mis-delete tail shards when that count is wrong). Keeping one
block in one dkey is what makes deletion a single punch and publication a single
atomic update. Large blocks are still fine — the `"d"` extent may span several
`recx` in the *same* update — they simply stay under one dkey.

Enumerating the dkeys of the k shared objects (`daos_obj_list_dkey`) lists every
block in the namespace, which is all a **manual offline GC** needs to purge
selectively (see
[Reclamation](#reclamation)). Partial
(per-layer / per-chunk) access is **not** part of the P1 data model; when
introduced in P4 it is a ranged fetch of the `"d"` extent alongside `"m"`, still
one RPC. (The caller derives the sub-range offsets from the layout it already
knows, so the ranged fetch still needs no prior lookup.)

### Read path and miss detection

A `get` has no index to consult, so the fetch itself must answer both “is it
there?” and “is it the right thing?”. The read is one `daos_obj_fetch` under
**`DAOS_COND_DKEY_FETCH`** requesting `"m"` (with `iod_size = DAOS_REC_ANY`) and
`"d"` (extent `[0, buflen)`, where `buflen` is the caller's buffer):

- **Miss.** The conditional fetch returns **`-DER_NONEXIST`** when the dkey does
  not exist. This is the *only* thing treated as a miss.
- **Never infer a hit from bytes.** DAOS reports zero output length for an
  unfound record (see [daos_obj.h](../../src/include/daos_obj.h)), so a
  non-conditional fetch of an absent key succeeds with an untouched buffer. A
  caller that ignores this hands uninitialized memory back as a cache hit — the
  precise failure this design forecloses. The library therefore requires the
  conditional flag, and additionally rejects a returned `"m"` of size 0 or a `"d"`
  output length short of `size` as `-DER_IO`.
- **Validate, then return.** The stored header is compared against the caller's
  expectation — identity hash, `meta`, `format_version` — and mismatches return
  `-DER_MISMATCH` (see [Key identity](#key-identity-and-validation)).
- **Size is authoritative from `"m"`, not from the buffer.** The caller does not
  know `size` before the fetch (there is no index to ask), so it requests its own
  buffer length. If the block turns out to be larger, the call returns
  `-DER_REC2BIG` with the required `size` reported in `*size`, and the partial
  contents of the buffer are undefined and must be discarded; the caller retries
  with a large enough buffer. In practice this is rare, because KV blocks are a
  fixed size for a given model and layout — which the caller already knows.

`exists` is the same fetch restricted to `"m"` with `sgl = NULL` and
`iod_size = DAOS_REC_ANY`: a size-only probe that moves no block bytes.

### Example: a block and its dkey

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

That key addresses one dkey in one shared object, holding both akeys:

```
key "a3f19c…e7"
   │   (compute (object, dkey) locally — no index lookup)
   ▼
 object O[ hash_hi(K) mod k ]        # e.g. O[2] of the k shared objects
   └── dkey "a3f19c…e7"
         ├── akey "m" → { model: llama-3-8b, dtype: bf16, size: 2097152,
         │                layout: paged-v1, created: … }
         └── akey "d" → the 2 MiB of bf16 K/V bytes  (recx [0, 2097152))
```

- **get:** compute `(object, dkey)`; issue **one** `daos_obj_fetch` for akeys
  `"m"` + `"d"` under `DAOS_COND_DKEY_FETCH`. Absent dkey → `-DER_NONEXIST` (a
  clean miss); present → validate `"m"`, return `"d"`. One round trip, data and
  metadata together.
- **put:** compute `(object, dkey)`; issue **one** `daos_obj_update` writing `"m"`
  + `"d"` under `DAOS_COND_DKEY_INSERT`. If the dkey already exists this is a
  dedup hit (no write); the conditional insert is also the atomic publish point.
- **another request with the same prefix:** computes the **same** dkey → hit →
  writes nothing, reads the same bytes (cross-request/cross-node reuse).

The next block (tokens 16–31) is a **different dkey** (its hash chains in the
prefix through token 31), landing on its own dkey — possibly in a different
shared object — so blocks chain and reuse stops exactly where two sequences
diverge.

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
        daos_oclass_id_t object_class;   /* shared block object class      */
        daos_size_t      chunk_size;     /* "d" akey extent chunk size      */
        uint32_t         shard_count;    /* k: number of shared objects     */
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
  a record `{ block_oids[k], object_class, chunk_size, shard_count, format_version, hash_algo }`.
  A namespace is created on first open by allocating its `k` shared block OIDs and
  inserting the record with **`DAOS_COND_DKEY_INSERT`**; the single winner's entry
  is authoritative, and a loser discards its speculatively allocated OIDs and
  re-reads the winning entry. Concurrent first-opens are therefore safe without a
  distributed lock.
- **Open.** `daos_kvc_open` first **validates the container's layout type is
  `KVCACHE`** (else `-DER_MISMATCH`), then looks up (or conditionally creates) the
  named namespace and checks its object class, chunk size, and format/hash version
  against `opts`. The registry record is the single source of truth for a
  namespace's schema, so all clients agree on layout and version.

## Fetch latency and plugin integration

The reason for deterministic addressing is **fetch latency parity with a
hand-written native plugin**, without giving up correctness. A `get` is one `daos_obj_fetch`: the
client derives `(object, dkey)` from the key locally and asks for the `"m"` and
`"d"` akeys in a single RPC. There is **no index hop** (no “fetch the location,
then fetch the data”), no OID-allocation service, and no metadata service — the
same round-trip count as a plugin that stores raw bytes under a key-derived dkey,
but the metadata rides back in that same RPC so the read is also self-validating.

Because a block's address is a **pure function of its content-identity key**, the
mapping is naturally plugin-friendly:

- **NIXL.** A DAOS NIXL backend engine registers the block buffer (`registerMem`)
  and, on `postXfer`, translates the descriptor — carrying the content key in its
  metadata blob — directly into `(object, dkey, recx)` and issues one
  `daos_obj_fetch`/`update`. NIXL's descriptor model has no place for a
  side lookup, and none is needed here. See the NIXL plugin backends
  (`obj`, `posix`, `cuda_gds`, `hf3fs`, …) at
  <https://github.com/ai-dynamo/nixl/tree/main/src/plugins>.
- **LMCache / Mooncake / SGLang.** The connector computes the same
  `(object, dkey)` locally, so it maps its own key model onto DAOS with a single
  fetch/update per block and no coordination.

In short, deterministic addressing is what lets any framework reach a block in
one RPC from the key alone, which is the property a native DAOS plugin must have
to beat a generic one on latency.

### Hot blocks

Content addressing has a workload-specific consequence worth stating: a block is
one dkey, and a dkey lands on one placement group. KV caches concentrate reuse by
design — a shared system prompt is the *same* prefix for every request in the
fleet, so its first block is the hottest key in the namespace and cannot be spread
by increasing `k`. Two things keep this from becoming a bottleneck. Reads of a
replicated object can be served by any replica, so a replicated object class turns
replication into read fan-out for exactly the blocks that need it. More
importantly, the framework's own GPU and host tiers absorb the hottest prefixes
long before they reach DAOS — this store is the capacity tier, not the first line
of reuse. Confirming this under a shared-prefix workload is part of P0-proto.

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

The library stores the canonical identity as the `"m"` akey alongside the block's
bytes and **validates it on every hit**: on `get`, the stored header is compared
against the caller's expected identity. A mismatch is treated as a stable,
non-retryable error — reported as `-DER_MISMATCH` — rather than returning the
bytes, which turns an accidental collision (or a stale layout version) into a
safe failure instead of silent corruption. Callers must handle `-DER_MISMATCH`
explicitly; it is not resolved by retrying `put`, since a retry cannot replace an
existing key's value in place.

To make genuine collisions astronomically unlikely in the first place, the
storage **dkey is derived from both the content hash and a canonical digest of
`meta`** (not the content hash alone). Two blocks that differ in any identity
field therefore land on different dkeys and cannot collide.

**Comparing `meta` alone is not enough, and the header stores the full hash for
exactly that reason.** The common case is a fleet serving *one* model with *one*
layout and dtype, so every block carries an identical `meta`; two different token
prefixes that collided on the dkey would pass a `meta`-only comparison and the
wrong tensors would be served. `meta` catches a stale layout version or a
misconfigured client — it cannot catch a prefix collision within the same
configuration. The `"m"` header therefore also carries the **full-width identity
hash**, and `get` compares it byte-for-byte against the hash the caller derived.
This matters because the dkey is a *bounded-length encoding* of that hash: any
truncation chosen for key-size reasons weakens the address, while the full-width
comparison stays at the hash's full strength. With a strong hash at full width, a
surviving collision is cryptographically negligible; with truncation, this check
is what keeps a collision a **detected error** rather than silent corruption.

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

The design mostly **sidesteps** per-object cost: a namespace has only `k` shared
objects (a small fan-out), not one object per block, so there is no per-block
object-ID allocation, open/close, or destroy on the hot path. The remaining
policy is:

- **Fixed shared objects.** The `k` block objects are allocated once at namespace
  bootstrap (`daos_cont_alloc_oids()`, see
  [daos_cont.h](../../src/include/daos_cont.h)); their handles are opened once and
  cached for the life of the namespace handle.
- **Object-class and chunk-size policy.** A namespace fixes the shared-object
  class (redundancy/striping — replicated, EC, or striped) and the `"d"` extent
  chunk size at `daos_kvc_open` time. Unlike a native single-object plugin that
  hard-codes a no-redundancy class, redundancy here is a namespace choice.
- **Fan-out `k`.** `k` trades metadata concentration against spread: a larger `k`
  distributes dkeys and reduces per-object contention, a smaller `k` keeps
  enumeration (`daos_obj_list_dkey`) cheaper. It is fixed per namespace and tuned
  in P0-proto.
- **Delete by dkey punch.** Reclaiming a block is a `daos_obj_punch_dkeys` on its
  dkey (removing both akeys at once), batched during offline GC — no per-block
  object destroy (see
  [Reclamation](#reclamation)).

Because blocks share objects and are addressed by dkey, blocks are effectively
**packed** — many per object — without the accounting a packed byte-range layout
would need: a dkey is its own allocation unit, so there are no offsets to track,
no tombstones for freed ranges, and no compaction. The correctness gaps a
key-string-only plugin exhibits (undetected miss, size/config confusion, leaked
shards on delete) are closed by the co-located `"m"` header and whole-dkey
deletion.

## Deduplication

Deduplication is implemented **entirely client-side** and does not depend on the
DAOS server-side dedup container property, which is not currently relied upon.

Deduplication reuses the content-derived key: because the key is derived from the
block's full identity, identical blocks produced by different requests or nodes
map to the same key and are stored exactly once. A **put** whose key already
exists writes no data (dedup hit); a **get** returns the existing bytes after
validating the stored header (see
[Key identity](#key-identity-and-validation)).

**A dedup hit on `put` does not read the stored header.** The conditional insert
fails without fetching, which is what keeps a hit cheap — but it also means a
put cannot detect that the resident block was written with a different identity.
That is not a correctness gap, because the mismatch is caught on the `get` that
would consume it; it only shifts detection to read time. Callers that would
rather fail fast (for example while rolling out a new layout version) can pass a
verify-on-hit flag, trading an extra `"m"`-only fetch for immediate detection.

## Atomic publication

Even in an immutable store, a `put` must not let a reader observe a half-written
block. Here publication is a **single conditional update** — there is no separate
data object to write first and no multi-object transaction:

1. Derive the block's content-identity key and its `(object, dkey)` address.
2. **Publish.** Issue one `daos_obj_update` writing the `"m"` and `"d"` akeys
   under the dkey with **`DAOS_COND_DKEY_INSERT`**. Both akeys land in a single
   atomic update, so a reader never sees `"d"` without a matching `"m"`, or either
   one half-written.
3. **Dedup / race outcome.** If the dkey already exists the conditional insert
   fails with **`-DER_EXIST`**, which the library reports as a **dedup-hit
   success**, not an error: the block is already published (a byte-identical copy,
   since the key is its content), so the `put` is a no-op. Concurrent writers of
   the same key therefore converge on one dkey with no lost or torn write, and a
   retried `put` after a timeout is safe for the same reason.

**No crash orphans.** Because data and metadata are one atomic update under one
dkey, a writer that dies mid-`put` either committed the dkey or did not — there is
no separately-allocated data object left dangling, so the design needs **no**
pending-write list and no orphan sweep. There are no lifecycle states,
generations, leases, or pins to leave inconsistent.

## Reclamation

Reclamation is where a native DAOS backend can do something a POSIX or generic
object backend structurally cannot, so it is specified in two stages: an
**interim client-side mechanism** that works today with no DAOS change, and the
**target design** — an access-driven, server-side sweep proposed as a staged DAOS
feature.

The store is **immutable**: a published block is never mutated. This removes the
entire class of delete-versus-read races a mutable shared store faces — there are
no pins, holders, leases, reference counts, lifecycle states, generations, or
resurrection, and **reads never write metadata**. Everything below rests on one
property: blocks are **regenerable**, so reclaiming a block that is still wanted
costs a cache miss and a recompute, never data loss or corruption.

### Why client-side reclamation does not scale

Two approaches were considered and rejected, and it is worth recording why,
because both look reasonable until the costs are counted.

**Enumerate and delete.** Enumeration (`daos_obj_list_dkey`) yields dkeys but no
timestamps, so selecting by age costs one `"m"` fetch per block — millions of
small RPCs on a large namespace. Parallel enumeration across the `k` objects
(`daos_obj_anchor_split`, see [daos_obj.h](../../src/include/daos_obj.h)) and
pipelining the metadata fetches help, but the cost stays proportional to the
number of blocks, and deleting while traffic runs means racing live readers.

**Generational rotation and time-partitioned namespaces.** Writing into a current
generation and reclaiming by punching whole older generations is O(1) per
generation instead of O(blocks) — but a `get` then has to probe more than one
generation to find a block, which **destroys the single-RPC read** the design
exists to provide (see
[Fetch latency](#fetch-latency-and-plugin-integration)). Paying read latency on
every request to make reclamation cheap is the wrong trade for a cache.

**Insert-time TTL is also wrong**, however it is implemented. A block's age says
nothing about its value: a shared system prompt written once at startup and read
by every request since is precisely the block an age-based policy discards first.
Reclamation has to be driven by **access**, not by insertion.

### Interim: bulk purge and manual offline GC

Until the server-side feature below exists, capacity is an **operational
responsibility** and reclamation is coarse and explicit:

- **Bulk purge (primary).** Destroy or punch the `k` shared block objects and, if
  desired, recreate them empty. A small, fixed number of DAOS operations that
  frees everything at once.
- **Manual offline GC (operator-triggered).** Purge all, or delete an explicit
  list of keys, punching each block's dkey with `daos_obj_punch_dkeys` (removing
  `"m"` and `"d"` together). Punching an absent key is a no-op, so GC is safely
  restartable.

**Safety contract.** Offline GC is **self-enforcing** rather than a soft operator
promise: the maintenance path opens the container in **exclusive mode**
(`DAOS_COO_EX`, see [daos_cont.h](../../src/include/daos_cont.h)), which succeeds
only if no other client has the container open (else `-DER_BUSY`) and blocks new
opens while held, so the store is provably quiesced. Run non-exclusively, the
blast radius is still small: the only hazard is punching a dkey a reader is
mid-fetch from, which surfaces as a retriable error the caller treats as a miss.

### Target: access-driven server-side reclamation

The observation that makes this cheap is that **the fetch RPC already arrives at
the exact target that owns the dkey and would evict it**. The dkey hash pins a
block *and every access to it* to one place, so recency can be recorded there, in
volatile memory, at no cost — no persistent write, no extra RPC, and no
cross-node coordination. What is normally a distributed-LRU problem collapses
into a local one.

Two existing DAOS facts make it cheaper still. The tree walk is **already being
paid for**: aggregation traverses OBJ → DKEY → AKEY continuously under a
credit/yield budget (`vos_aggregate()`, see
[vos_aggregate.c](../../src/vos/vos_aggregate.c)). And reclaim already has a
deferred drain: a punch feeds the GC bins consumed by `vos_gc_pool()` (see
[vos_gc.c](../../src/vos/vos_gc.c)). Expiry is therefore a **predicate added to a
sweep that already runs**, reusing the reclaim path that already exists.

The design has three parts:

**1. Volatile heat (a reference bit).** Each target keeps a reference-bit array in
VOS thread-local storage, following the pattern already used for the object cache
(`vos_obj_cache_create`, see [vos_common.c](../../src/vos/vos_common.c), built on
[lru.c](../../src/common/lru.c)). The bit is indexed by a hash of the container
and dkey and sized to a configured memory budget — roughly 12 MiB per 100 million
blocks per target. Hash collisions make a cold block look hot, which only *delays*
reclaim; they can never cause a wrong deletion.

**2. The bit is set on fetch *and* on put.** Setting it on fetch is what makes the
policy access-driven while keeping `get` a pure read. Setting it on put matters
for a subtler reason: a `put` whose key is already resident writes nothing (a
dedup hit), so without this a block that is continuously re-offered would still
look untouched and could be evicted out from under the writers that keep
proposing it.

**3. A CLOCK sweep, riding aggregation.** Using aggregation's existing iteration
and credit/yield model, each dkey is judged:

```
if age < CACHE_MIN_AGE            -> skip          (protects fresh writes)
else if reference bit is set      -> clear, skip   (second chance)
else if under reclaim pressure    -> punch dkey    (-> GC bins -> vos_gc_pool)
```

A block that keeps being read keeps setting its bit, so it survives an unbounded
number of passes; a block nobody touches is reclaimed on a subsequent pass. This
is second-chance CLOCK, and it gives recency **without a single write on the read
path**. Because the sweep runs inside VOS on the target that owns the data, the
punch is a **local operation: no client RPC, no DTX, and no enumeration** — the
costs that make client-side reclamation untenable simply do not arise.

Policy is configured with new container properties — `CACHE_MODE`
(off / watermark / ttl), `CACHE_MIN_AGE`, `CACHE_HIGH_WM` and `CACHE_LOW_WM`, and
an optional `CACHE_TTL` backstop — registered exactly like the `KVCACHE` layout
type (see [Build Integration](#build-integration)). Watermark mode is the
headline: the sweep reclaims only under space pressure and stops once usage falls
back to the low watermark, which is how a cache should behave.

#### Consequences and limits

- **No read-time filtering is needed** in watermark or CLOCK mode: a block is
  present until it is punched, so the read path is unchanged. Only the optional
  hard `CACHE_TTL` backstop would require a deterministic read-time check against
  the record's epoch.
- **Restart loses the heat.** After a restart every block looks cold, so the sweep
  observes a warm-up window before it may reclaim, with `CACHE_MIN_AGE` as the
  backstop; heat rebuilds from live traffic.
- **Replicas may diverge in presence, never in content.** Reads spread across
  replicas, so each sees only a subset of accesses and replicas will evict at
  different times. This is safe *only* because blocks are immutable and
  content-addressed: a reader gets the correct bytes or a miss. This is the
  property that permits uncoordinated, target-local eviction; a mutable store
  could not do it.
- **Erasure coding is excluded** from this phase. Punching an EC data cell locally
  without updating parity would corrupt the stripe, so access-driven reclaim is
  enabled for replicated and `SX` classes only.
- **Timestamps come from the record, not the client.** The `created` field in
  `"m"` is advisory and subject to fleet clock skew; the server-side sweep instead
  uses each record's own VOS epoch, which is an HLC value derived from the
  physical clock and convertible to wall time (`d_hlc2sec`, see
  [gurt/common.h](../../src/include/gurt/common.h)). No new per-record metadata is
  required for age.

#### Later: frequency and admission control

A single reference bit distinguishes touched from untouched but cannot rank two
cold blocks, and it does nothing about *admission*: a one-shot request's blocks
can still displace a fleet-wide shared prefix. A later phase replaces the bit with
a small frequency sketch (TinyLFU-style, on the order of a few bits per block) and
adds an **admission check on `put`** — which is already a write, so the read path
stays untouched. Priority remains **client-supplied**: the library routes blocks to
tiers with different `CACHE_*` policies based on prefix depth, which is a function
of the key's identity and therefore known at both put and get time, so routing
costs nothing and `get` stays one RPC. Server-side policy stays content-agnostic;
it never interprets application data.

`exists()` is **advisory only** — a hint for admission decisions, not a lock. A
block may be reclaimed between `exists()` and `get()`, which the caller handles as
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
 * library base32/hex-encodes the hash into a NUL-free block dkey and stores/
 * validates meta (the "m" akey) on every hit. */
typedef struct {
        const void          *hash;      /* raw content-hash bytes            */
        size_t               hash_len;
        const daos_kvc_meta_t *meta;    /* model/shape/dtype/layout identity */
} daos_kvc_key_t;

/* Whole-block operations (P1). Immutable store: put writes once (a resident key
 * is reported as a dedup-hit success, not an error); get reads. No holder/pin
 * arguments — reads never take a lease and never mutate metadata. get requests
 * the caller's buffer length as the "d" extent and reports the block's true size
 * from the "m" header in *size: a bigger block returns -DER_REC2BIG with the
 * required size and undefined buffer contents. A missing key is -DER_NONEXIST
 * (never a zero-filled buffer). Buffers in sgl are owned by the caller and must
 * remain valid until the operation completes (ev fires, or the call returns in
 * blocking mode). */
int daos_kvc_put(daos_handle_t kvch, const daos_kvc_key_t *key,
                 const d_sg_list_t *sgl, uint64_t flags, daos_event_t *ev);
int daos_kvc_get(daos_handle_t kvch, const daos_kvc_key_t *key,
                 d_sg_list_t *sgl, size_t *size, daos_event_t *ev);
/* Advisory presence hint; an "m"-only size probe that moves no block bytes.
 * A block may be reclaimed between exists() and get(), which the caller handles
 * as a miss. */
int daos_kvc_exists(daos_handle_t kvch, const daos_kvc_key_t *key,
                    bool *exists, daos_event_t *ev);

/* Manual, offline reclamation (maintenance) — run when deletion is safe (see
 * Reclamation). For a race-free purge, open the maintenance handle on a
 * container opened DAOS_COO_EX (exclusive), which fails with -DER_BUSY if any
 * other client has the container open. purge punches/destroys the k shared
 * objects; delete punches a specific set of block dkeys. There are no
 * separately-allocated data objects, so there are no crash orphans to sweep. */
int daos_kvc_purge(daos_handle_t kvch, daos_event_t *ev);
int daos_kvc_delete(daos_handle_t kvch, uint32_t n, const daos_kvc_key_t *keys,
                    int *item_rcs, daos_event_t *ev);

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
- Issues many per-block reads/writes **in parallel** (parallel batching). Each
  block is one `daos_obj_fetch`/`daos_obj_update` on its `(object, dkey)`, so
  per-block I/Os run concurrently across the `k` shared objects and their
  targets; they are not merged into one larger transfer.
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
- **Commit is the conditional dkey insert.** A put is durable once its
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

The **data path is client-side only**. It composes a small set of shared DAOS
objects, multi-akey `daos_obj_fetch`/`daos_obj_update`, conditional inserts
(`DAOS_COND_DKEY_INSERT`), dkey enumeration (`daos_obj_list_dkey`), and dkey punch
(`daos_obj_punch_dkeys`) — all with full existing server-side support, and no
distributed transactions, leases, background services, or engine/VOS changes. The
only cross-component piece for the shipping design is registering the `KVCACHE`
container layout type, a control-plane/tooling change with no storage-engine
impact (see [Build Integration](#build-integration)).

**Access-driven reclamation is the one proposed server-side feature**, staged
separately (see [Reclamation](#reclamation)). It is deliberately additive and
leans on machinery that already exists rather than introducing a new service:

- a volatile per-target reference-bit array in VOS thread-local storage, following
  the existing object-cache pattern (`vos_obj_cache_create`, see
  [vos_common.c](../../src/vos/vos_common.c));
- a predicate on the iteration aggregation already performs, under its existing
  credit/yield budget ([vos_aggregate.c](../../src/vos/vos_aggregate.c));
- reclaim through the existing punch → GC-bin → `vos_gc_pool()` drain
  ([vos_gc.c](../../src/vos/vos_gc.c));
- new `CACHE_*` container properties, registered like any other container
  property.

It adds **no new RPC, no DTX, and no durable format change** — record age comes
from each record's existing VOS epoch. It is scoped to replicated and `SX` object
classes; erasure coding is excluded because a local cell punch would corrupt
parity.

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
- **Access-driven reclamation** (staged feature, see
  [Reclamation](#reclamation)) adds `CACHE_*` container properties before
  `DAOS_PROP_CO_MAX` in `src/include/daos_prop.h`, with the matching range check
  in `src/common/prop.c` and control-plane/CLI plumbing; the sweep predicate lands
  in `src/vos/vos_aggregate.c`, the volatile heat array alongside the VOS TLS
  object cache in `src/vos/vos_common.c`, and reclaim reuses `src/vos/vos_gc.c`
  unchanged.
- Any optional GPU-direct dependency is wired through `site_scons/components` and
  guarded in the `kvcache` `SConscript`, matching how other optional
  prerequisites are handled. The default host-staging path has no extra
  dependency.

## Phased Roadmap

- **P0 — Design/spec** (this document) plus an API header sketch.
- **P0-proto — De-risking prototype (spike, gates P1).** A small prototype that
  validates the mechanisms most likely to be wrong before full implementation:
  - **Single-RPC publication + dedup** — one multi-akey `daos_obj_update`
    (`"m"` + `"d"`) under `DAOS_COND_DKEY_INSERT`; confirm no reader observes a
    half-written block, a dedup hit writes nothing, and concurrent writers of the
    same key converge on one dkey.
  - **Crash recovery** — kill a writer mid-`put` and confirm that, because data
    and metadata are one atomic dkey update, the dkey is either fully present or
    absent — there is no dangling data object and nothing to sweep. No poisoned or
    partial entries.
  - **Offline GC correctness** — validate bulk purge (punches/destroys the `k`
    shared objects), selective delete (by key list and by `created` age via
    `daos_obj_list_dkey` + `"m"`), and both safety modes: **exclusive-mode** GC
    (`DAOS_COO_EX` open fails with `-DER_BUSY` if anyone else holds the container,
    so purge is race-free), and non-exclusive GC where a mid-read dkey punch
    yields a retriable read error (never corruption) and a re-`put` reproduces
    byte-identical data.
  - **Fetch latency + fan-out** — confirm a `get` is one multi-akey fetch and
    measure it against a raw single-object plugin; tune `k` for dkey spread and
    per-object contention, and measure `daos_obj_list_dkey` enumeration cost.
  - **Miss and mismatch detection** — confirm an absent key returns
    `-DER_NONEXIST` under `DAOS_COND_DKEY_FETCH` (never an untouched buffer
    reported as a hit), an oversized block returns `-DER_REC2BIG` with the true
    size, and a forced dkey collision under identical `meta` is caught by the
    full-width hash compare. Fixes the dkey width for P1.
  - **Hot shared prefix** — drive a workload where every request shares a system
    prompt and measure the hottest dkey's placement group under replicated and
    non-replicated object classes, to confirm read fan-out plus upper-tier caching
    keeps it off the critical path.
  - **Reclamation cost baseline** — measure what makes client-side reclamation
    untenable, to size the P6 feature: enumeration throughput over the `k` objects
    (including `daos_obj_anchor_split` parallel anchors), the per-block `"m"` fetch
    cost of age selection, and multi-dkey punch cost when dkeys span shards.
  - **Typed container + bootstrap** — `daos_kvc_cont_create` sets the container
    layout type to `KVCACHE` and creates the registry root at its fixed OID; open
    rejects a non-`KVCACHE` container; concurrent namespace first-open uses an
    idempotent conditional insert with loser cleanup and schema/version checks.
  The prototype is not shipped.
- **P1 — Core C backend**: first-class `KVCACHE` container type with typed
  `daos_kvc_cont_create` (creates the container and its registry root); `k` shared
  block objects addressed by a content-identity dkey with co-located `"m"`
  (metadata: `size`, `meta`, `created`) and `"d"` (bytes) akeys, single-RPC
  multi-akey fetch/update publication committed by a single conditional insert,
  client-side deduplication with hit-time identity validation over a versioned
  canonical `meta` serialization, the host-staging I/O engine (parallel batching,
  resource-limited admission), and reclamation via bulk purge + manual offline GC
  (`daos_kvc_purge` / `daos_kvc_delete`).
- **P2 — Python bindings**: `DKVCache` class and DLPack tensor hand-off.
- **P3 — GPU-direct (deferred)**: host staging is the only committed path.
  Zero-copy-to-GPU is gated on first selecting and prototyping a concrete
  DAOS-compatible device-memory path (`FI_HMEM` transport support or a
  DFS/cuFile backend); no zero-copy work is scheduled until that selection is
  made.
- **P4 — Partial reuse**: versioned range descriptor and ranged put/get for
  per-layer/per-chunk access (not part of the P1 surface or verification).
- **P5 — Connectors**: vLLM/LMCache first, then SGLang, Mooncake, and NIXL.
- **P6 — Access-driven reclamation (DAOS feature proposal).** The staged
  server-side sweep described in [Reclamation](#reclamation): `CACHE_*` container
  properties, a volatile per-target reference-bit array set on fetch and put, a
  CLOCK predicate riding aggregation's existing iteration, and reclaim through the
  existing punch → GC drain. Scoped to replicated and `SX` classes. Independent of
  P2–P5 — the interim bulk purge and offline GC keep P1 shippable without it.
- **P7 — Frequency and admission.** Replace the reference bit with a small
  frequency sketch and add a put-time admission check so cold one-shot blocks
  cannot displace hot shared prefixes; add client-routed priority tiers. Depends
  on P6 and on its measurements showing boundary thrashing.
- **Future work (unscheduled)** — explore an optional node-local shared cache
  (for example POSIX shared memory) in front of DAOS; out of scope for this
  design.

Phases P2, P3, and P4 can proceed in parallel once P1 lands. P1 does not start
until P0-proto validates single-RPC publication, crash recovery, offline GC, and
fetch latency / fan-out. Connectors (P5) depend on the Python bindings (P2). P6
is independent of P2–P5 and is sequenced against DAOS release planning rather
than this library's phases; P7 depends on P6.

## Verification

- **API review** — the header compiles standalone and the surface is reviewed
  against the vLLM/LMCache `StorageBackend` and Mooncake Store interfaces to
  confirm it can be mapped without gaps.
- **Performance** — microbenchmarks measure throughput (GB/s) and operation rate
  against a `pydaos` `DDict`/`DArray` baseline for representative block sizes, and
  confirm a `get` is a single multi-akey fetch whose latency matches a raw
  single-object plugin.
- **Correctness** — tests cover: deduplication (a put whose key exists writes
  nothing and reports a hit; identical content across nodes stores one copy);
  **miss detection** (a fetch of an absent key returns `-DER_NONEXIST` under
  `DAOS_COND_DKEY_FETCH` and never an untouched, zero-filled buffer reported as a
  hit); **oversized read** (a block larger than the caller's buffer returns
  `-DER_REC2BIG` with the true size, never a silent truncation); hit-time identity
  validation (a stale layout version *and* a forced dkey collision under identical
  `meta` both return `-DER_MISMATCH` via the full-width hash compare, never silent
  corruption); single-RPC publication (no reader observes a block without its
  header); addressing-parameter immutability (opening with a different `k`,
  `hash_algo`, or `format_version` returns `-DER_MISMATCH` instead of silently
  re-hashing); cross-language identity (the versioned canonical `meta`
  serialization + named hash produce byte-identical keys across
  languages/versions); and typed container creation + namespace first-open
  (`daos_kvc_cont_create` sets layout type `KVCACHE` and the registry root; open
  rejects a non-`KVCACHE` container with `-DER_MISMATCH`; concurrent first-open
  uses an idempotent conditional insert with loser cleanup and schema/version
  compatibility).
- **Reclamation (interim)** — bulk purge punches/destroys the `k` shared objects;
  selective offline GC punches exactly the chosen block dkeys (both akeys);
  **exclusive-mode** (`DAOS_COO_EX`) GC refuses to run while any other client holds
  the container (`-DER_BUSY`), and in non-exclusive mode the mid-read contract
  holds — punching a dkey under a concurrent reader yields a retriable error, not
  corruption, and a re-`put` reproduces byte-identical data.
- **Reclamation (access-driven, P6)** — the acceptance criterion is the one
  insert-time expiry fails: a **continuously read block survives an unbounded
  number of sweep passes** while cold blocks around it are reclaimed. Alongside
  it: usage driven past `CACHE_HIGH_WM` returns to `CACHE_LOW_WM` within a bounded
  number of sweeps under sustained write load; `get` latency and target CPU are
  unchanged with the sweep active versus disabled (setting the reference bit must
  not be measurable); hammering put/get on a block at the eviction boundary
  confirms the put-as-access rule prevents a dedup-hit-then-miss recompute storm;
  a target restarted under load does not mass-evict during the warm-up window; a
  deliberately undersized bit array only delays reclaim and never deletes a block
  that was referenced; and the sweep sharing aggregation credits does not starve
  normal aggregation.
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
   shape, dtype, layout version, layer/chunk, adapter context), and both the
   full-width hash and `meta` are validated against the stored `"m"` header on
   every hit.
2. **Storage layout** — blocks are dkeys in `k` shared objects, with data and
   metadata co-located as `"d"`/`"m"` akeys; `k` (fan-out) is the main tunable,
   traded off with `daos_obj_list_dkey` enumeration cost.
3. **Dkey width** — how much of the identity hash the dkey encodes, trading key
   size against collision rate. Truncation is safe (the full-width hash in `"m"`
   turns a collision into a detected `-DER_MISMATCH`) but a collision still costs
   a false miss and a recompute, so the width is chosen to make that rare.
4. **Reclamation model** — bulk purge + manual offline GC today, with capacity as
   an **operational responsibility**; access-driven server-side reclaim (P6) is the
   target design and the main DAOS differentiator. Open within it: whether the
   sweep piggybacks aggregation (reuses its walk and throttling, but couples the
   two) or runs as a separate scrubber-style pass (isolated, but duplicates the
   walk); and whether a hard `CACHE_TTL` backstop is worth the read-time epoch
   check it would require.
5. **Recency granularity** — one reference bit is the cheapest mechanism that
   fixes insert-time expiry, but cannot rank two cold blocks or govern admission.
   Boundary thrashing in P6 measurements is the trigger to pull the frequency
   sketch (P7) forward.
6. **Offline GC concurrency** — the recommended, race-free mode opens the
   container **exclusively** (`DAOS_COO_EX`), guaranteeing no concurrent readers
   during purge/GC; running GC non-exclusively alongside live traffic is also
   supported, trading that guarantee for occasional retriable read misses on the
   blocks being deleted (no correctness impact, since blocks are regenerable and
   immutable).
7. **Hot shared prefixes** — a universally shared prefix block is a single dkey on
   a single placement group and cannot be spread by raising `k`; whether replica
   read fan-out plus the framework's own upper tiers are sufficient is measured in
   P0-proto.
8. **Buffer contract** — how far to push registered/acquired buffers on callers
   (fast path) versus accepting arbitrary buffers with a bounce copy, given
   framework buffer-ownership constraints.
9. **GPU-direct dependency** — deferred; no zero-copy-to-GPU path is committed
   until one is selected and prototyped against DAOS: `FI_HMEM` transport support
   versus a DFS/cuFile backend. Host staging is the only committed path.
