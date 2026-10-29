---
id: flow-pool-create
kind: flow
related: [pool, mgmt, rsvc, rdb, vos, control]
---
# Flow: `dmg pool create`

Spans three processes and both RPC worlds: gRPC (dmg -> daos_server), dRPC (daos_server ->
daos_engine) and CaRT (engine <-> engine). Modules: [pool](../modules/pool.md),
[vos](../modules/vos.md); dRPC table: [generated/drpc.md](../generated/drpc.md).

```mermaid
sequenceDiagram
  participant D as dmg (Go)
  participant S as daos_server MS leader (Go)
  participant E as daos_engine (local to MS leader)
  participant T as all pool ranks (engines)
  participant R as pool-service replicas (engines)
  D->>S: gRPC MgmtSvc.PoolCreate
  S->>S: pick ranks, fault domains, sizes; sysdb.AddPoolService (state Creating, Raft)
  S->>E: dRPC DRPC_METHOD_MGMT_POOL_CREATE (207)
  E->>T: CaRT MGMT_TGT_CREATE (collective)
  T->>T: vos_pool_create per target
  E->>R: CaRT RSVC_START (collective) -> rdb_create
  E->>R: CaRT POOL_CREATE to new leader
  R->>R: ds_pool_create_handler: init_pool_metadata in rdb tx
  E-->>S: PoolCreateResp (svc replicas, target ranks)
  S->>S: ps.State = Ready; sysdb.UpdatePoolService
  S-->>D: PoolCreateResp
```

## dmg (admin node)

1. `src/control/cmd/dmg/pool.go::poolCreateCmd.Execute`: parses flags (size/ratio/ranks/props),
   builds `control.PoolCreateReq`.
2. `src/control/lib/control/pool.go::PoolCreate` -> `src/control/lib/control/pool.go::poolCreateGenPBReq`
   converts to `mgmtpb.PoolCreateReq` (`src/proto/mgmt/pool.proto::PoolCreateReq`) and invokes gRPC
   `MgmtSvc.PoolCreate` (`src/proto/mgmt/mgmt.proto`) through `InvokeUnaryRPC`, which routes MS
   requests to the management-service leader.

## daos_server, management service leader

3. `src/control/server/mgmt_pool.go::mgmtSvc.PoolCreate`: leader check, serialised request.
4. `src/control/server/mgmt_pool.go::mgmtSvc.poolCreate`:
   - `poolCreateAddSystemProps`, takes a pool lock (`sysdb.TakePoolLock`); if the pool already
     exists and is not Ready, returns `TryAgain`;
   - chooses ranks (all joined ranks, a random subset for `--nranks`, or the explicit list;
     non-joined ranks become `UnavailableRanks` / DOWNOUT), compresses the fault-domain tree,
     computes per-tier sizes (`src/control/server/mgmt_pool.go::calculateCreateStorage`);
   - records the pool in the MS Raft DB with state Creating:
     `src/control/system/raft/database.go::Database.AddPoolService` ->
     `src/control/system/raft/raft.go::Database.submitPoolUpdate`;
   - `svc.harness.CallDrpc(ctx, daos.MethodPoolCreate, req)` (`src/control/server/harness.go::CallDrpc`)
     to a local engine; on failure destroys the pool and removes the MS entry;
   - on success sets `ps.Replicas`, `PoolServiceStateReady`, `sysdb.UpdatePoolService`.

## daos_engine that received the dRPC

5. `src/mgmt/srv.c` dispatches `DRPC_METHOD_MGMT_POOL_CREATE` to
   `src/mgmt/srv_drpc.c::ds_mgmt_drpc_pool_create` (unpacks `Mgmt__PoolCreateReq`, props, ACL).
6. `src/mgmt/srv_pool.c::ds_mgmt_create_pool`:
   1. `src/mgmt/srv_pool.c::ds_mgmt_tgt_pool_create_ranks` sends collective `MGMT_TGT_CREATE`
      to all active ranks. Each rank runs `src/mgmt/srv_target.c::ds_mgmt_hdlr_tgt_create`
      (preallocates files, `tgt_create_preallocate`) and, per target,
      `src/mgmt/srv_target.c::tgt_vos_create_one` -> `src/vos/vos_pool.c::vos_pool_create`.
   2. `src/mgmt/srv_pool.c::ds_mgmt_pool_svc_create` ->
      `src/pool/srv_pool.c::ds_pool_svc_dist_create`: builds the initial pool map buffer
      (`pool_buf`, version 1), chooses service replicas from the redundancy factor, then:
      - `src/rsvc/srv.c::ds_rsvc_dist_start` broadcasts `RSVC_START`; each replica runs
        `src/rsvc/srv.c::ds_rsvc_start_handler` -> `src/rsvc/srv.c::ds_rsvc_start` -> `start()` ->
        `src/rdb/rdb.c::rdb_create` (new Raft DB, backed by VOS) and starts Raft;
      - sends `POOL_CREATE` to the elected leader via `rsvc_client`.
   3. On the pool-service leader `src/pool/srv_pool.c::ds_pool_create_handler` opens an rdb
      transaction; `src/pool/srv_pool.c::init_pool_metadata` writes the pool map and properties,
      `ds_cont_init_metadata()` initialises the container-service KVSs; `rdb_tx_commit()`.
   4. Failures roll back by destroying the targets created in step 1.
7. Response (service replicas, target ranks, tier sizes) returns via dRPC to step 4.

## Related flows

- Pool destroy mirrors this: `DRPC_METHOD_MGMT_POOL_DESTROY` -> `ds_mgmt_drpc_pool_destroy()`.
- After create, the pool service leader steps up (`src/pool/srv_pool.c::pool_svc_step_up_cb`) and
  distributes the map to targets via IV (`ds_pool_iv_map_update()`).
- Pool connect from a client: `dc_pool_connect()` -> `POOL_CONNECT` ->
  `src/pool/srv_pool.c::ds_pool_connect_handler`.
