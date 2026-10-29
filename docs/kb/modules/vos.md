---
id: vos
kind: module
layer: server/storage
depends_on: [bio, vea, common, dtx]
used_by: [object, dtx, container, rebuild, rdb, mgmt, chk, pipeline]
related: [epoch, ilog, aggregation, dtx]
---
# VOS: Versioning Object Store (`src/vos`)

Design doc: [src/vos/README.md](../../../src/vos/README.md). Public API:
`src/include/daos_srv/vos.h`, types: `src/include/daos_srv/vos_types.h`.

## Purpose

Local, per-target persistent store for one pool shard. Stores every object/dkey/akey/value
version by epoch (MVCC), answers "what was visible at epoch E", records punches, persists local
DTX state, and reclaims old versions by aggregation. Metadata lives in SCM (or DRAM + WAL in
MD-on-SSD mode) through `umem`; bulk array data goes to NVMe via `bio` with space from `vea`.
VOS has no network code; callers (object, dtx, rebuild, rdb) run on the owning target xstream.

## File map

| File | Role |
|---|---|
| `src/vos/vos_pool.c` | Pool create/open/close/query, per-pool space and GC setup |
| `src/vos/vos_container.c` | Container create/open/close/destroy, per-container DTX tables |
| `src/vos/vos_io.c` | Fetch/update begin/end, SCM/NVMe space reservation and publish |
| `src/vos/vos_obj.c`, `src/vos/vos_obj_cache.c`, `src/vos/vos_obj_index.c` | Object punch/hold, DRAM object cache, object index (OI) tree |
| `src/vos/vos_tree.c` | dkey/akey btree classes and single-value tree |
| `src/vos/evtree.c`, `src/vos/evt_iter.c` | Extent-versioned tree for array values |
| `src/vos/ilog.c`, `src/vos/vos_ilog.c` | Incarnation log (create/punch history per entity) |
| `src/vos/vos_ts.c` | Timestamp cache for read/write conflict detection |
| `src/vos/vos_dtx.c`, `src/vos/vos_dtx_iter.c` | Local DTX table: attach, check, commit, abort, iteration |
| `src/vos/vos_aggregate.c` | Aggregation and discard |
| `src/vos/vos_iterator.c`, `src/vos/vos_query.c` | Generic nested iterator; key queries |
| `src/vos/vos_gc.c` | Background garbage collection of freed trees/extents |
| `src/vos/vos_layout.h` | Persistent (on-media) structure definitions |
| `src/vos/vos_internal.h`, `src/vos/vos_tls.h` | Internal types, per-xstream TLS |
| `src/vos/vos_common.c` | Module registration (`vos_srv_module`), init, standalone mode |

## Entry points (all in `src/include/daos_srv/vos.h`)

- Pool/container: `vos_pool_create()`, `vos_pool_open()`, `vos_cont_create()`, `vos_cont_open()`.
- Update: `src/vos/vos_io.c::vos_update_begin` -> (caller fills buffers via `bio_iod_prep()` /
  bulk) -> `src/vos/vos_io.c::vos_update_end`. One-shot: `vos_obj_update()`.
- Fetch: `src/vos/vos_io.c::vos_fetch_begin` -> `vos_fetch_end()`; one-shot `vos_obj_fetch()`.
- Punch: `src/vos/vos_obj.c::vos_obj_punch`.
- Iterate: `src/vos/vos_iterator.c::vos_iterate` (or `vos_iter_prepare()`/`vos_iter_probe()`/
  `vos_iter_next()`/`vos_iter_fetch()`).
- Aggregate/discard: `src/vos/vos_aggregate.c::vos_aggregate`, `src/vos/vos_aggregate.c::vos_discard`.
- DTX: `src/vos/vos_dtx.c::vos_dtx_attach`, `src/vos/vos_dtx.c::vos_dtx_check`,
  `src/vos/vos_dtx.c::vos_dtx_commit`, `src/vos/vos_dtx.c::vos_dtx_abort`.

## Key structures

- Persistent: `src/vos/vos_layout.h::vos_pool_df`, `src/vos/vos_layout.h::vos_cont_df`,
  `src/vos/vos_layout.h::vos_obj_df`, `src/vos/vos_layout.h::vos_dtx_act_ent_df`,
  `src/vos/vos_layout.h::vos_dtx_cmt_ent_df`, `src/vos/vos_layout.h::vos_dtx_blob_df`.
- DRAM: `src/vos/vos_internal.h::vos_pool`, `src/vos/vos_internal.h::vos_container`,
  `src/vos/vos_io.c::vos_io_context`.

## Data layout

```
vos pool -> container table -> container -> object index (OI) -> object
         -> dkey btree -> akey btree -> single-value btree (SV) | evtree (EV, arrays)
```

- Each object, dkey, and akey has an ilog recording create/punch epochs; visibility at an epoch
  is decided by the ilog first, then by the value trees.
- SV tree is keyed by epoch; reads return the newest version <= read epoch.
- evtree stores (extent, epoch) rectangles; reads compute visible extents at the read epoch.
- Values are stored inline in SCM when small; larger array data goes to NVMe (`bio` addresses,
  space from `vea_reserve()` via `src/vos/vos_io.c::vos_reserve_blocks`).

## Threading

- VOS state is per target and only touched from the owning target xstream; no locks across xstreams.
- Aggregation is driven by the container module's per-container ULT
  (`src/container/srv_target.c::cont_start_agg` -> `src/container/srv_target.c::cont_vos_aggregate_cb`)
  and yields periodically (`src/vos/vos_aggregate.c::vos_aggregate_yield`).

## Invariants and gotchas

- Everything is epoch-versioned; never overwrite in place. Punch is a versioned tombstone.
- Do not yield (ABT yield, blocking I/O, RPC wait) inside a `umem` transaction.
- Conflicts surface as `-DER_INPROGRESS` (hit an uncommitted DTX; caller should refresh/retry)
  and `-DER_TX_RESTART` (MVCC conflict; client restarts the transaction with a new epoch).
- `vos_update_begin()` reserves space; it is only published in `vos_update_end()`; errors must still
  call `*_end()` so reservations are cancelled.
- Changing anything in `src/vos/vos_layout.h` is an on-media format change: requires a layout
  version bump and upgrade handling.

## Tests

- Unit: `src/vos/tests` (`vos_tests` binary: `vts_io.c`, `vts_dtx.c`, `vts_aggregate.c`,
  `vts_ilog.c`, `vts_mvcc.c`, ...). Run via `utils/run_utest.py`.
- Perf: `src/tests/vos_perf.c`.

## Where to look for...

| Question | Look at |
|---|---|
| Why a fetch returned `-DER_INPROGRESS` | `src/vos/vos_dtx.c` (`vos_dtx_check_availability()`) |
| How array extents become visible | `src/vos/evtree.c` |
| Space accounting / ENOSPACE | `src/vos/vos_space.c` |
| Persistent format | `src/vos/vos_layout.h` |
