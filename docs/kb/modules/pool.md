---
id: pool
kind: module
layer: client+server/metadata
depends_on: [rsvc, rdb, rebuild, container, vos, bio, mgmt, object, security, engine, cart]
used_by: [object, container, dtx, rebuild, mgmt, chk, security]
related: [pool_map, iv, rsvc, rdb]
---
# Pool (`src/pool`)

Overview: [src/pool/README.md](../../../src/pool/README.md). Client header:
`src/include/daos/pool.h`; server header: `src/include/daos_srv/pool.h`; pool map:
`src/include/daos/pool_map.h` (implementation `src/common/pool_map.c`). Opcodes:
[generated/rpcs.md](../generated/rpcs.md#pool).

## Purpose

Three things live here:

1. **Pool service** (`srv_pool.c`): replicated metadata service (rsvc over rdb/Raft) on a few
   replica ranks. Stores pool map, properties, ACL, attributes, handles; serves connect /
   disconnect / query / set-prop / exclude / drain / reintegrate / extend; schedules rebuild
   when the map changes. The container service is hosted in the same rdb.
2. **Per-target pool state** (`srv_target.c`): `ds_pool` per engine and `ds_pool_child` per
   target xstream (open VOS pool handle, local map version, GC/scrub/flush ULTs).
3. **Client** (`cli.c`): `dc_pool_*` connect/query/exclude, pool map cache, leader discovery.

## File map

| File | Role |
|---|---|
| `src/pool/cli.c` | Client API (`dc_pool_connect`, `dc_pool_disconnect`, `dc_pool_exclude`, ...), map cache `dc_pool_map_update` |
| `src/pool/srv_pool.c` | Pool service: create/load/start (`ds_pool_svc_dist_create`, `ds_pool_svc_start`), all client-facing handlers (`ds_pool_connect_handler`, `ds_pool_query_handler`, `ds_pool_update_handler`, ...), map updates `pool_svc_update_map` |
| `src/pool/srv_target.c` | `ds_pool` / `ds_pool_child` lookup and lifecycle, server-side pool handles |
| `src/pool/srv_iv.c` | IV keys for pool map, props, connection handles (`ds_pool_iv_map_update`, ...) |
| `src/pool/srv_pool_map.c` | Target state transitions in the map |
| `src/pool/srv_layout.c`, `src/pool/srv_layout.h` | rdb key layout of the pool service, global/layout versions |
| `src/pool/srv_pool_chkpt.c`, `src/pool/srv_pool_scrub_ult.c` | MD-on-SSD checkpointing, checksum scrubbing ULTs |
| `src/pool/srv_pool_check.c` | Checker (`dmg check`) integration |
| `src/pool/srv.c` | Module registration (`pool_module`) |
| `src/pool/rpc.h`, `src/pool/rpc.c` | RPC formats and opcode lists |

## Key structures

- `src/include/daos_srv/pool.h::ds_pool`: per-engine pool (map, map version, IV namespace).
- `src/include/daos_srv/pool.h::ds_pool_child`: per-target pool (VOS pool handle, map version,
  background ULT requests).
- `src/include/daos_srv/pool.h::ds_pool_hdl`: server-side connection handle.
- `src/include/daos/pool.h::dc_pool`: client pool (map cache, rsvc client for leader hint).
- `src/include/daos/pool_map.h::pool_map`, `pool_domain`, `pool_target`, `pool_component`.
- `src/pool/srv_internal.h`, `src/pool/srv_pool.c` (`struct pool_svc`): service state.

## Pool map and target states

`PO_COMP_ST_NEW`, `UP` (being reintegrated/extended), `UPIN` (healthy), `DOWN` (excluded, rebuild
pending/running), `DOWNOUT` (excluded and rebuilt), `DRAIN` (being drained). Every change bumps
the map version; the new map is distributed with IV (`ds_pool_iv_map_update()`) and targets
refresh (`ds_pool_child_map_refresh_async()`). Rebuild is scheduled from the pool service
(`ds_rebuild_schedule()`).

## Threading

- Pool service (leader and replicas), rdb and IV handlers run on the system xstream (xs 0).
- `ds_pool_child` work (VOS, GC, scrubbing, checkpoint) runs on each target xstream.
- Clients find the leader via `rsvc_client` hints; non-leaders reply `-DER_NOTLEADER`.

## Invariants and gotchas

- Map versions are monotonic. RPCs carry the sender's map version; stale senders get
  `-DER_STALE` and must refresh.
- All pool-service metadata changes go through an rdb transaction (`rdb_tx_begin()` ->
  `rdb_tx_commit()`) on the current leader term; a term change aborts in-flight work.
- `DAOS_POOL_GLOBAL_VERSION` / layout versions gate upgrades (`dmg pool upgrade`); changing the
  rdb layout requires a version bump in `src/pool/srv_layout.h`.
- Do not perform per-target VOS work on xs 0; use `ds_pool_task_collective()` /
  `dss_thread_collective()` style helpers to fan out to targets.

## Tests

- `src/tests/suite/daos_pool.c`, `src/tests/suite/daos_mgmt.c`, `src/tests/suite/daos_rebuild.c`,
  ftests under `src/tests/ftest/pool`.

## Where to look for...

| Question | Look at |
|---|---|
| Pool creation end-to-end | [flows/pool-create.md](../flows/pool-create.md) |
| What happens on `dmg pool exclude` | `src/pool/srv_pool.c::ds_pool_update_handler` -> `pool_svc_update_map` |
| How clients find the leader | `src/pool/cli.c::dc_pool_choose_svc_rank`, `src/common/rsvc.c` |
| Per-target pool startup | `src/pool/srv_target.c::ds_pool_child_start` |
