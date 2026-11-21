# Server-Side EC for GPU Direct I/O (Draft) Design

## Summary

- **Problem**: GPU direct I/O ([gpuio.md](gpuio.md)) rejects erasure-coded (EC)
  objects, because the client computes parity with the CPU and the CPU cannot read GPU memory.
  DFS files are EC by default in redundant containers, so GPU I/O on them fails by default.
- **Proposal**: let GPU direct I/O use EC objects without the client ever reading the buffer:
  1. **Array updates** always take the existing partial-stripe path: data cells go to the data
     targets and the same bytes go to every parity target as replicas. EC aggregation computes
     parity later, on the server.
  2. **Array fetches** and **small single values** need no change while all data targets are
     healthy.
  3. **Degraded fetch**, which decodes on the client, fails with a visible error in phase 1
     and is supported after phase 2, which reconstructs the data on the server.
  4. **Updates of large single values** are rejected in phase 1 and are supported after phase
     3, which encodes them on the parity targets at write time. Fetches of them work from
     phase 1.
- **Impact**: phase 1 is a client change only, with no wire or on-disk change. The price is
  write bandwidth: the client sends `k * (1 + p)` cells per stripe instead of `k + p`, the
  same as `RP_(p+1)`, so EC only saves space, and only after aggregation.

## Background

### GPU Direct I/O

GPU direct I/O ([gpuio.md](gpuio.md)) lets a caller pass GPU buffers to the object, array and
DFS `_ma` calls. The client CPU cannot read or write these buffers, so every client path that
touches the data with the CPU is skipped or rejected: inline transfer, data checksums, iov
merging, and EC.

### Why EC Matters

In a container with a redundancy factor of 1 or more and at least 3 fault domains, an array
object created without an explicit class is EC (`dc_set_oclass()`). That includes DFS files.
The DFS `_ma` calls, which the NIXL backend ([integrations.md](integrations.md)) needs,
therefore fail by default in redundant containers. Today the creator has to force a replicated
or non-redundant class, and existing EC files cannot be read into or written from GPU memory.

### Client-Side EC Today

The client splits each update into per-target I/O descriptors in `obj_ec_req_reasb()`
(`cli_ec.c`):

- `obj_ec_recx_scan()` classifies each recx. A recx that covers at least one full stripe is
  encoded. The parts before and after the full stripes are partial, and each data cell of a
  partial part is also sent to every parity target as a replica (`ec_partial_tgt_recx_nrs`).
- `obj_ec_recx_reasb()` builds the per-target sgl. For data cells and partial replicas,
  `ec_data_seg_add()` inserts **references** to the user iovs into the segment sorter, so no
  data is copied. For full stripes, `ec_parity_seg_add()` inserts client-allocated parity
  buffers (`oer_pbufs`).
- `obj_ec_encode()` → `obj_ec_recx_encode()` → `obj_ec_stripe_encode()` reads the data cells
  with the CPU (ISA-L, plus a `memcpy` for cells that span iovs) to fill `oer_pbufs`.
- Large single values (`obj_ec_singv_req_reasb()`) are split evenly over the data targets and
  encoded on the client (`obj_ec_singv_encode()`). Small single values go to one data target
  (`obj_ec_singv_small_idx()`) plus all parity targets, without encoding.

On the server, EC aggregation (`srv_ec_aggregate.c`) runs in the background on the leader
parity shard of each stripe (`agg_process_stripe()`):

- When the replicas on the parity target form a full stripe, it encodes them locally
  (`agg_encode_local_parity()`), pushes parity to the other parity targets
  (`agg_peer_update()`) and removes the replicas (`agg_update_vos()`).
- When there is parity and newer partial replicas exist, it updates the parity
  (`agg_process_partial_stripe()`, which may fetch data cells from peers).
- When there is no parity and the replicas do not form a full stripe, it does nothing, so
  the replicas stay ("No parity, partial-stripe worth of replica, nothing to do").
- Single values are skipped (`agg_akey()` returns `VOS_ITER_CB_SKIP` for
  `VOS_ITER_SINGLE`).

A fetch reads the data cells from the data targets. When a data target is unavailable, the
client sends the request to a parity shard with `ORF_EC_DEGRADED`
(`obj_ec_valid_shard_get()` path in `cli_obj.c`). The parity shard serves what it can from
replicas. For the rest, the client starts a recovery task (`obj_ec_recov_cb()`) that fetches
k cells into host buffers, decodes them (`obj_ec_recov_data()`) and copies the result into the
user sgl (`obj_ec_recov_fill_back()` → `obj_ec_sgl_copy()`).

### Why GPU I/O Rejects EC

`dc_obj_fetch_task()` and `dc_obj_update()` return `-DER_NOTSUPPORTED` for
`DAOS_OBJ_IO_GPU_DIRECT` on EC objects (`cli_obj.c`, the two `obj_is_ec(obj)` checks).
The CPU touches the user buffer only on these paths:

- full-stripe array encode (`obj_ec_stripe_encode()`)
- large single-value encode (`obj_ec_singv_encode()`)
- degraded-fetch recovery fill-back (`obj_ec_sgl_copy()`)

The rest of the EC path, including the replicas of partial stripes, only moves iov
references. There is a second, independent blocker: `obj_rw_bulk_prep()` drops the memory
attributes when the bulk is built from the EC reassembled sgl (`req_reasbed` and
`sgls == orr_sgls`), because that sgl can hold host parity buffers. A GPU buffer would then be
registered as host memory.

## Goals and Non-Goals

### Goals

- GPU direct update and fetch of array values on EC objects, with no CPU access to GPU memory.
- The cases left out of the first phase are supported by later phases: degraded fetch after
  phase 2, updates of large single values after phase 3.
- No change to the wire protocol or the persistent layout for arrays.
- Non-GPU EC I/O is unchanged.
- Every case that is not supported fails with a visible error, and every degraded case is
  logged. Nothing falls back silently.

### Non-Goals

- Client-side encoding on the GPU (see [A.1](#a1-client-encode-on-the-gpu)).
- Data checksums and dedup. They are still rejected for GPU I/O, as in the main design. New
  containers have checksums on by default, so a GPU update also needs a container created
  with checksums off, with or without this design.
- Transactions. GPU I/O still cannot be converted to a transaction.
- Making EC writes as cheap as client-side encoding. They are not (see
  [Network and Space Cost](#network-and-space-cost)).

## Design

### Overview

```
client (GPU buffer)                     servers
  dc_obj_update(GPU_DIRECT, EC)
    obj_ec_req_reasb(force partial)
      data cells  ------------------->  data targets   (VOS)
      replicas    ------------------->  parity targets (VOS, replica extents)
                                          |
                                          | EC aggregation (background, leader parity)
                                          v
                                        parity cells written, replicas removed

  dc_obj_fetch(GPU_DIRECT, EC)
      data cells  <-------------------  data targets
      degraded    <-------------------  parity shard: replicas, or
                                        phase 2: server-side reconstruction
```

### Array Updates: Replicate Now, Encode Later

A new reassembly flag, `orr_no_encode` in `struct obj_reasb_req`, is set when
`ORF_GPU_DIRECT` is set on an EC update. In `obj_ec_recx_scan()`, every recx then takes the
`start >= end` (partial) branch, so:

- `ec_partial_tgt_recx_nrs()` counts one replica recx per parity target, and one recx per
  data target that the range covers.
- `oer_recxs` stays empty, so `obj_ec_recx_reasb()` never sees a full stripe:
  `ec_data_seg_add(..., add_parity = true)` references the user iovs for the data targets and
  for every parity target.
- `obj_ec_encode()` has no stripe to encode and touches nothing.

A recx that spans many stripes now produces one replica recx per parity target that is many
stripes long. The server stores it as an ordinary replica extent, and aggregation works stripe
by stripe, encoding each full stripe locally on the leader; it already handles extents that
cross a stripe boundary (`agg_extent_add()`, `agg_carry_over()`). This is expected to work,
but it is not tested with extents written this long in one update, and no other server path
was checked for an assumption that a replica extent is shorter than one stripe (see
[Testing & Validation](#testing--validation)).

### Bulk and Memory Attributes

When `orr_no_encode` is set, the reassembled sgl (`orr_sgls`) contains only references into
the user sgl (no `oer_pbufs`), so it is all GPU memory. `obj_rw_bulk_prep()` passes the
memory attributes for the reassembled sgl in this case:

- **Registration per I/O**: each segment is registered with the GPU memory type. The
  reassembled sgl has one segment per data cell per stripe for the data targets, plus the
  replica segments for the parity targets, which have the same addresses as the data. If the
  transport registers each segment on its own (to be confirmed for verbs), a 1 MiB update on
  4+2 with 128 KiB cells is about 10 registrations where a replicated object needs 1. The
  transport MR cache returns an existing registration for a repeated address; without the cache
  (verbs clients, `CRT_MRC_ENABLE` unset) every segment is registered on every I/O.
- **`ma_handle`**: `dc_obj_mem_attr_ctx_prep()` registers the region once per context, and
  every segment hits the cache, as for replicated objects. EC GPU I/O should therefore use
  `daos_mem_register()`; without it the cost above applies.

A mixed sgl (GPU data plus host parity buffers) can never happen, because no parity buffer is
allocated. This rule is also why large single values cannot keep client encoding with a GPU
buffer (see [Single Values](#single-values)).

### Single Values

`OBJ_EC_SINGV_EVENDIST_SZ(k)` is `(k / 8 + 1) * 4096`, which gives 4 KiB for k < 8 and
8 KiB for 8 ≤ k < 16 (`obj_ec.h`).

- **Small** (`obj_ec_singv_one_tgt()` true): `obj_ec_singv_req_reasb()` copies only the iov
  descriptors and sends the whole value to one data target and to every parity target. No
  change is needed, apart from passing the memory attributes as above.
- **Large**: the layout depends only on `iod_size`. The server (`obj_singv_ec_rw_filter()`,
  `srv_obj.c`) and every reader expect `k` data cells plus `p` parity cells. Storing a large
  value on one target would therefore break readers
  (see [A.4](#a4-large-single-value-on-one-target)), and aggregation does not cover single
  values, so replicate-now/encode-later does not apply.

Phase 1 returns `-DER_NOTSUPPORTED` for a GPU update of a large single value on an EC object.
This does not block the target consumers: DFS file data and `daos_array` use array values,
and DFS uses single values only for metadata that is never in GPU memory.

**GPU updates of large single values are supported once phase 3 is done.** It is built only
if a consumer needs it, and it encodes on the server:

- The client sends no parity, plus a new flag (`ORF_EC_SRV_ENCODE`).
- Each data target pulls its own cell, as today.
- Each parity target pulls the whole value into a DRAM buffer, encodes it with the
  container's codec (the ISA-L tables that aggregation uses, `ae_codec`) and stores only its
  own parity cell.
- The DTX leader is already a parity shard, so the update stays atomic under the existing
  DTX, with no new commit logic.
- Cost: each parity target pulls `k` cells, which is the same network cost as an array.

### Fetch

A healthy EC fetch reads data cells from the data targets with the per-target offsets built
by `obj_ec_recx_reasb()`. `obj_ec_fetch_set_sgl()` only sets `sg_nr_out`, and the iov
merging in `cli_obj.c` is already skipped for `ORF_GPU_DIRECT`. Lifting the fetch-side
rejection therefore needs only the bulk memory attributes. A fetch of a large single value
reads data cells only, so it can be allowed in phase 1, for values written by a host client.

### Degraded Fetch

The first step is unchanged and safe for GPU memory: the client redirects the I/O to a parity
shard with `ORF_EC_DEGRADED`. If the replicas there are newer than the parity, the parity
shard serves them directly through the bulk.

The second step, the client recovery task, is not safe for GPU memory. It is used when the
parity shard returns a recovery list (`orw_rels`), or for snapshot reads
(`DIOF_EC_RECOV_SNAP`).

- **Phase 1**: `obj_ec_recov_cb()` fails with `-DER_NOTSUPPORTED` when `ORF_GPU_DIRECT` is
  set and logs the object and the failed target. The caller (a NIXL backend or an application)
  can retry with a host buffer. DAOS does not stage through host memory itself, so the
  fallback stays visible.
- **Phase 2**: server-side reconstruction. The client sets a new flag (`ORF_EC_SRV_RECOV`)
  together with `ORF_EC_DEGRADED`. Instead of returning `orw_rels`, the parity shard rebuilds
  the missing cells into DRAM with the server-side client (`dsc_obj_fetch()` with
  `DIOF_EC_RECOV`, as migration does in `srv_obj_migrate.c`), then pushes them to the
  client's GPU bulk. The latency of a degraded read becomes one extra server round trip. The
  CPU and memory cost moves to the server.

### Network and Space Cost

The figures below use `k + p` per stripe, with the default 128 KiB cell
(`DAOS_EC_CELL_DEF`):

- **Client NIC, write**: `k * (1 + p)` cells instead of `k + p`. That is 2x for 4+2 and 2.4x
  for 8+2, and exactly the bandwidth of `RP_(p+1)`.
- **Server, after aggregation**: the leader parity encodes from local replicas and pushes
  `p - 1` parity cells to its peers. That is extra east-west traffic and CPU, spread over
  targets by the EC layout rotation.
- **Space before aggregation**: `(1 + p)` copies, the same as `RP_(p+1)`. Aggregation is
  skipped while the pool is rebuilding (`agg_process_stripe()`), so space does not shrink
  during rebuild.
- **Tail stripes**: a stripe that is never fully written keeps its replicas forever. With 4+2
  and 128 KiB cells the stripe is 512 KiB, so a 256 KiB KV block written as a single file is
  never encoded. It is stored as `RP_3`, but with EC read semantics.
- **Reads**: same as client-encoded EC.

Assessment: for write-heavy KV-cache offload, EC through this path buys space only, and only for
blocks of at least one stripe that survive until aggregation. For short-lived blocks, a
replicated container (`RP_2` or `RP_3`) is simpler and costs the same or less on the wire.
This design is worth it mainly so that existing EC DFS containers can be read and written with
GPU buffers, not as the recommended layout for new KV-cache containers.

### Checksums, Transactions and Conditional Operations

- **Checksums**: still rejected for GPU updates. Aggregation would recompute parity
  checksums on the server, but the data cells have no client checksum.
- **Transactions**: still rejected (`obj_auxi->tx_convert` check). A transactional EC update
  would need the same no-encode reassembly in `obj_tx.c`, which is out of scope.
- **Conditional operations**: the existence checks do not touch data buffers, so they keep
  working. To be confirmed by test.

### Observability

- Client: phase 1 logs GPU EC updates sent without encoding (debug, in `obj_ec_req_reasb()`),
  rejected degraded fetches and rejected updates of large single values. Counters come with the
  client counters of the main design, a follow-up change.
- Server: the existing `opm_update_ec_partial` counter (`srv_ec.c`) already counts these
  updates, because the server sees partial updates.

## Implementation Phases

Phase 1 is phase 4 of the main design: it follows the DFS and array `_ma` APIs, which are
unusable on files with the default class in redundant containers without it.

### Phase 1: Arrays (client only)

- Add `orr_no_encode`, set it for `ORF_GPU_DIRECT` on EC updates, and handle it in
  `obj_ec_recx_scan()`.
- Pass memory attributes for reassembled sgls when `orr_no_encode` is set, or for a fetch.
- Replace the two `obj_is_ec()` rejections with:
  - large single-value update: `-DER_NOTSUPPORTED`
  - `obj_ec_recov_cb()` with `ORF_GPU_DIRECT`: `-DER_NOTSUPPORTED`
- Add logs (counters follow with the main design).
- Update `gpuio.md` (safety check 4, the object class bullet of DFS and Array APIs,
  Errors and Fallback, Implementation Phases, the EC rejection test, the "EC by default" risk
  and Future Works) and `integrations.md` (rule 6 and phase 3).

### Phase 2: Server-Side Degraded Reconstruction

- `ORF_EC_SRV_RECOV` flag, reconstruction on the parity shard, push to the client bulk.
- Gate it on the server version, because an older server ignores the flag (see
  [Wire Protocol](#wire-protocol)).

### Phase 3 (optional): Server Encode for Large Single Values

- `ORF_EC_SRV_ENCODE`, a whole-value pull on the parity targets, and encoding in the update
  path. After it, a GPU update of a large single value on an EC object is supported.
- Built only if a consumer stores large single values in GPU memory.

## Compatibility & On-Disk Impact

### Persistent Layout

No change. Replica extents and parity extents already exist and are aggregated by the
current code.

### Wire Protocol

- Phase 1: none. The server sees ordinary partial-stripe updates and normal fetches.
  `ORF_GPU_DIRECT` already exists (`obj_rpc.h`).
- Phases 2 and 3: new `orw_flags` bits. An old server would silently ignore them (phase 2
  would return `orw_rels`, and phase 3 would store the wrong cells), so the client must only
  set them when every engine supports them. This needs a `DAOS_OBJ_VERSION` bump, or a pool
  or container feature bit, decided when phase 2 starts.

### Backward Compatibility

Objects written by GPU clients are ordinary EC objects. Host clients and old clients read
them unchanged. Non-GPU I/O takes the same path as today.

## External Interfaces

No new public API. The behavior of GPU I/O (the `_ma` calls with device memory) changes on EC
objects:

- array update and fetch: supported from phase 1
- degraded fetch that needs decoding: `-DER_NOTSUPPORTED` until phase 2, then supported
- large single-value update: `-DER_NOTSUPPORTED` until phase 3, then supported

## Testing & Validation

- **Unit tests** (`cli_ec` / reassembly, host memory and `orr_no_encode` set): for single,
  multi-stripe and misaligned recxs, check that the segment counts, the per-target recxs and
  the `siod` offsets match the partial-path model, and that no `oer_pbufs` are allocated.
- **Functional tests** (`daos_test` EC group, host buffers with a test-only fault flag that
  forces `orr_no_encode`):
  - write, aggregate (`DAOS_FORCE_EC_AGG`), read back, and check the parity against a
    client-encoded copy
  - multi-stripe replicas on the parity targets
  - tail stripe that never aggregates
  - degraded read before and after aggregation
  - rebuild of an object with pending replicas
- **GPU tests** (a node with a GPU): the same cases with CUDA buffers, with and without
  `ma_handle`. Check that the degraded fetch error is reported.
- **Regression**: the existing EC suites with host buffers, unchanged.
- **Performance**: GPU write throughput on EC 4+2 and 8+2 against `RP_2` and `RP_3`, with and
  without the MR cache, plus the aggregation CPU and lag under steady writes.

## Risks, Mitigations and Future Works

### Risks and Mitigations

- **Replicas larger than a stripe on a parity target** may hit an unverified assumption in
  the server or in aggregation. Mitigation: phase 1 tests with forced no-encode on host
  buffers, before any GPU run.
- **Space until aggregation**: `(1 + p)` copies, forever for tail stripes. A parity target
  holds `k` cells per stripe where a data target holds one, so parity targets fill faster,
  and aggregation is skipped while the pool rebuilds. Mitigation: measure the aggregation lag
  under sustained GPU writes, recommend cell sizes that match the block size; parity rotation
  spreads the load across targets.
- **Registration amplification** without a registered region (one registration per cell).
  Mitigation: `daos_mem_register()`, which is already recommended for GPU I/O.
- **Degraded reads fail** in phase 1 while a target is down. Mitigation: the caller retries
  with host buffers. Phase 2 removes the limitation.

### Future Works

- Checksums for GPU I/O on EC, which depend on the checksum work in the main design.
- An aggregation hint (for example, aggregate as soon as a full stripe of replicas exists) to
  shorten the time spent at `(1 + p)` space.

## Alternatives Considered

### A.1 Client Encode on the GPU

Encode parity on the GPU (CUDA kernels or a GPU EC library), then send data and parity as
with host buffers. This has the lowest network cost (`k + p`), but `libdaos` would have to
link CUDA, manage streams and GPU memory for parity, and keep the GPU and ISA-L codecs
bit-identical. The main design keeps `libdaos` free of CUDA, so this was rejected for now. It
could be done later, in a consumer that already owns a CUDA context (NIXL), which would pass
the parity buffers in.

### A.2 Host Staging on the Client

Copy the GPU data to host memory (through the CPU with GDRCopy or `cudaMemcpy`) and use the
normal EC path. It works for every case, but adds a copy and a CUDA dependency, and is exactly
the silent bounce that GPU direct I/O exists to avoid. It stays the caller's choice, not
DAOS's.

### A.3 Server Encode for Arrays at Write Time

Parity targets pull the full stripes and encode them during the update, like phase 3 for
single values. The network cost is the same as replicate-now/encode-later, but it adds new
update-path code, DRAM buffers and CPU on the latency path, and it does not handle partial
stripes. Aggregation already does this job in the background, so it was rejected.

### A.4 Large Single Value on One Target

Store a large GPU single value like a small one (one data target plus parity replicas). The
readers decide the layout from `iod_size` (`obj_ec_singv_one_tgt()`), so a host reader would
look for `k` cells and fail. It would need a persistent per-value layout marker, which is an
on-disk change, so it was rejected.

## References

- [GPU Direct I/O design](gpuio.md)
- `src/object/cli_ec.c`: `obj_ec_req_reasb()`, `obj_ec_recx_scan()`, `obj_ec_recx_reasb()`,
  `ec_data_seg_add()`, `obj_ec_singv_req_reasb()`, `obj_ec_recov_data()`
- `src/object/cli_obj.c`: `obj_rw_bulk_prep()`, `obj_ec_recov_cb()`, EC rejections in
  `dc_obj_fetch_task()` and `dc_obj_update()`
- `src/object/srv_ec_aggregate.c`: `agg_process_stripe()`, `agg_encode_local_parity()`,
  `agg_akey()`
- `src/object/srv_obj.c`: `obj_singv_ec_rw_filter()`, degraded fetch handling
- `src/object/srv_obj_migrate.c`: server-side recovery fetch with `dsc_obj_fetch()`
