---
id: concepts
kind: overview
related: [architecture]
---
# DAOS concepts mapped to code

Definitions: [docs/overview/terminology.md](../overview/terminology.md),
[docs/overview/storage.md](../overview/storage.md). This page maps each concept to where it lives in code.

## Storage model

| Concept | Meaning | Code anchors |
|---|---|---|
| System | Set of `daos_server`s sharing a fabric and a management service | `src/control/system` |
| Rank | One `daos_engine` instance in the system (`d_rank_t`) | `src/include/daos/pool_map.h::pool_domain` |
| Target | Unit of storage and concurrency inside an engine: one main xstream, one VOS pool file per pool | `src/include/daos/pool_map.h::pool_target` |
| Pool | Storage reservation across a set of targets; has a pool map and a replicated pool service | `src/include/daos_srv/pool.h::ds_pool`, `src/pool/cli.c::dc_pool_connect` |
| Pool map | Versioned tree of fault domains -> ranks -> targets with per-component state (`PO_COMP_ST_UP`, `UPIN`, `DOWN`, `DOWNOUT`, `DRAIN`, `NEW`) | `src/common/pool_map.c`, `src/include/daos/pool_map.h::pool_map` |
| Pool service | Raft-replicated metadata service for a pool (leader on one replica); also hosts container service | `src/pool/srv_pool.c::ds_pool_svc_start`, [modules/pool.md](modules/pool.md) |
| Container | Object namespace inside a pool; metadata in the pool service rdb; per-target state in `ds_cont_child` | `src/include/daos_srv/container.h::ds_cont_child`, `src/container/srv_container.c` |
| Object | 128-bit OID (`daos_obj_id_t`); object class encodes redundancy (RP/EC) and sharding | `src/include/daos_obj_class.h`, `src/object/obj_class.c` |
| Shard | One placement unit of an object, `daos_unit_oid_t` = OID + shard index, lives on one target | `src/include/daos/object.h::daos_unit_oid_t` |
| Redundancy group | Set of shards that replicate or EC-protect the same dkeys | `src/object/obj_layout.c` |
| dkey / akey | Distribution key (selects redundancy group, unit of co-location) / attribute key under a dkey | `src/include/daos_obj.h` |
| Value | Single value (SV, atomic blob) or array value (EV, extents of records) | `src/include/daos_obj.h::daos_iod_type_t` |
| Layout | OID + pool map -> shard placement, computed client- and server-side | `src/placement/pl_map.c::pl_obj_place` |

## Versioning, consistency, transactions

| Concept | Meaning | Code anchors |
|---|---|---|
| Epoch | 64-bit HLC timestamp; every update is versioned by epoch (MVCC) | `src/include/daos_types.h::daos_epoch_t`, `src/gurt/hlc.c` |
| Snapshot | Persistent epoch that aggregation must preserve | `src/container/srv_container.c` |
| DTX | Distributed transaction used for every replicated/EC modification; leader + non-leaders | [modules/dtx.md](modules/dtx.md) |
| CoS | Commit-on-Share: committable DTXs cached and piggybacked on later RPCs instead of extra commit RPCs | `src/dtx/dtx_cos.c` |
| Incarnation log (ilog) | Per object/dkey/akey log of create/punch epochs | `src/vos/ilog.c` |
| Punch | Logical delete at an epoch | `src/vos/vos_obj.c::vos_obj_punch` |
| Aggregation | Merges/removes old versions below the oldest snapshot (VOS) and computes EC parity (EC aggregation) | `src/vos/vos_aggregate.c::vos_aggregate`, `src/object/srv_ec_aggregate.c` |

## Runtime and services

| Concept | Meaning | Code anchors |
|---|---|---|
| Xstream / ULT | Argobots OS-thread / user-level thread. Never block an xstream; yield instead | `src/engine/ult.c::dss_ult_create`, `src/engine/sched.c` |
| rsvc | Generic replicated-service framework (used by the pool service, which also hosts the container service) on rdb | `src/rsvc/srv.c::ds_rsvc_start` |
| rdb | Raft-based replicated KV store backed by VOS | `src/rdb/rdb.c::rdb_create`, `src/rdb/rdb_tx.c` |
| IV | Incast variables: tree-based propagation/caching of values (pool map, props, handles) across ranks | `src/cart/crt_iv.c`, `src/pool/srv_iv.c`, `src/engine/server_iv.c` |
| SWIM | Gossip-based failure detection between engines | `src/cart/swim` |
| Rebuild | Re-protect data after pool map change (exclude, drain, reintegrate, extend) via object migration | `src/rebuild/srv.c`, `src/object/srv_obj_migrate.c` |
| Handle | Pool/container handles carry capabilities; global handles shareable via `l2g`/`g2l` | `src/pool/cli.c::dc_pool_local2global` |
| Task (client) | Every client API call is a `tse_task_t` in an event-driven scheduler | `src/common/tse.c` |
| Event | Async completion object for client calls | `src/client/api/event.c` |

## Media

| Concept | Meaning | Code anchors |
|---|---|---|
| SCM / pmem | Byte-addressable metadata (and small values) via PMDK-like allocators behind `umem` | `src/common/mem.c`, `src/common/dav`, `src/common/dav_v2`, `src/common/ad_mem.c` |
| MD-on-SSD | Metadata kept in DRAM and persisted via WAL + checkpoint on NVMe | `src/bio/bio_wal.c`, `src/pool/srv_pool_chkpt.c` |
| NVMe | Bulk data via SPDK blobstore; space allocated by VEA | `src/bio`, `src/vea/vea_api.c::vea_reserve` |
| SMD | Server metadata: which device holds which target/pool blob | `src/bio/smd` |
