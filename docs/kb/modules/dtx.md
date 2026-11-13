---
id: dtx
kind: module
layer: server/transaction
depends_on: [vos, container, pool, engine, placement]
used_by: [object, container, rebuild, vos]
related: [epoch, cos, object]
---
# DTX: distributed transactions (`src/dtx`)

Design: [docs/overview/transaction.md](../../overview/transaction.md). Server API:
`src/include/daos_srv/dtx_srv.h`; shared types/states: `src/include/daos/dtx.h`.

## Purpose

Makes every modification that touches more than one shard (replicas, EC data + parity, multi-dkey
transactions) atomic. One shard is the **leader**; it executes locally and forwards to
**non-leaders**, collects results, then commits or aborts. Commit is normally **asynchronous**:
the DTX becomes *committable*, is cached in the Commit-on-Share (CoS) table and committed later in
batches (or piggybacked to non-leaders on the next RPC). Local persistent state lives in VOS
(`src/vos/vos_dtx.c`).

## File map

| File | Role |
|---|---|
| `src/dtx/dtx_common.c` | Handle lifecycle (`dtx_begin`/`dtx_end`, `dtx_leader_begin`/`dtx_leader_end`), forwarding (`dtx_leader_exec_ops`), batched-commit / aggregation / cleanup ULTs, resend detection |
| `src/dtx/dtx_rpc.c` | Sending DTX_COMMIT / ABORT / CHECK / REFRESH (`dtx_commit`, `dtx_abort`, `dtx_check`, `dtx_refresh`), collective variants |
| `src/dtx/dtx_srv.c` | Module registration (`dtx_module`), RPC handlers `dtx_handler`, `dtx_coll_handler`, starts `dtx_batched_commit` ULT on every target |
| `src/dtx/dtx_cos.c` | CoS cache: add committable DTX, fetch batches, piggyback lists |
| `src/dtx/dtx_resync.c` | Resolve in-doubt DTXs after a leader/pool-map change (`dtx_resync`) |
| `src/dtx/dtx_coll.c` | Collective DTX (one DTX spanning many targets, e.g. object-wide punch) |
| `src/dtx/dtx_internal.h` | RPC list (`DTX_PROTO_SRV_RPC_LIST`), thresholds, internal types |

## Lifecycle on the write path

```
leader (target xstream)                         non-leader
dtx_leader_begin()
dtx_leader_exec_ops(dlh, obj_tgt_update, ...) --DAOS_OBJ_RPC_TGT_UPDATE--> ds_obj_tgt_update_handler
   local: obj_local_rw()                                                   dtx_begin()
                                                                           obj_local_rw()  (state: prepared)
                                                                           dtx_end()
dtx_leader_end()
   all ok + sync   -> vos_dtx_mark_committable + dtx_commit() now
   all ok + async  -> vos_dtx_mark_committable + dtx_cos_add() -> later batched commit / piggyback
   failure         -> dtx_abort()
```

- Leader side: `src/dtx/dtx_common.c::dtx_leader_begin`, `src/dtx/dtx_common.c::dtx_leader_exec_ops`,
  `src/dtx/dtx_common.c::dtx_leader_end`.
- Non-leader side: `src/dtx/dtx_common.c::dtx_begin`, `src/dtx/dtx_common.c::dtx_end`.
- Async commit: `src/dtx/dtx_cos.c::dtx_cos_add`, `src/dtx/dtx_common.c::dtx_batched_commit`,
  `src/dtx/dtx_rpc.c::dtx_commit`. Piggyback: `src/dtx/dtx_cos.c::dtx_cos_get_piggyback`.
- Solo DTX (single shard, e.g. non-replicated object): no forwarding; validated inside VOS.

## States (`src/include/daos/dtx.h`)

`DTX_ST_PREPARED` (locally applied, not yet decided) -> `DTX_ST_COMMITTABLE` (all participants
prepared; leader decided) -> `DTX_ST_COMMITTED`; or `DTX_ST_ABORTED`. Also `DTX_ST_CORRUPTED`,
and transient `DTX_ST_PREPARING` / `DTX_ST_COMMITTING` / `DTX_ST_ABORTING`.

## Key structures

- `src/include/daos_srv/dtx_srv.h::dtx_handle`: per-operation state (xid, epoch, flags, modified
  records, CoS lists). `dth_solo`, `dth_sync` select solo / synchronous commit.
- `src/include/daos_srv/dtx_srv.h::dtx_leader_handle`: leader extension with sub-request status.
- `src/include/daos/dtx.h::dtx_id`, `src/include/daos/dtx.h::dtx_memberships`,
  `src/include/daos_srv/vos_types.h::dtx_entry`.

## Threading

- `dtx_begin`/`dtx_leader_begin` run in the RPC handler ULT on the target xstream.
- Forwarding to non-leaders is done via `dss_chore_register()` (helper xstreams when configured).
- Per-target background ULTs: `dtx_batched_commit` (created with `dss_ult_create_all()`),
  `dtx_aggregate`, `dtx_cleanup` (created via `sched_create_ult()`).
- Resync runs per container from `src/container/srv_target.c::ds_dtx_resync`.

## Invariants and gotchas

- A reader hitting a prepared-but-undecided DTX gets `-DER_INPROGRESS`; the server first tries
  `dtx_refresh()` to ask the leader, then the client retries.
- Client resends are detected with `src/dtx/dtx_common.c::dtx_handle_resend`; never re-execute an
  update whose DTX already exists.
- The leader builds its DTX entry before forwarding/bulk transfer to avoid races with fast
  non-leaders (see comment in `src/object/srv_obj.c::obj_tgt_update`).
- Committable DTXs that fail to enter the CoS cache are recovered by the next resync.
- Pool map changes can change the leader; `dtx_resync()` resolves orphaned DTXs.

## Tests

- Unit: `src/dtx/tests` (`dtx_tests`).
- Integration: `src/tests/suite/daos_dist_tx.c`, `src/tests/suite/daos_obj.c`.
