# DAOS KB benchmark

Questions for measuring whether the knowledge base (`docs/kb`, `.github/copilot-instructions.md`,
`.github/instructions/`) makes an agent faster and more accurate. Every expected answer was
checked against the code; anchors are validated by `utils/kb/kb.py lint`.

## Protocol

1. Create two worktrees from the same commit:
   - **baseline**: delete `docs/kb`, `.github/copilot-instructions.md`, `.github/instructions`.
   - **kb**: keep them.

   In **both**, delete `utils/kb/benchmark` so the agent can't read the answers.
2. Ask each question in a **fresh chat** (same model, agent mode), verbatim, with no extra context.
3. Record per question: score, tool calls, wall time, and any hallucination (a function, file,
   opcode or behaviour that does not exist).
4. Compare the totals per category. The KB is useful if the kb run scores higher with fewer tool
   calls; any hallucination in the kb run that traces back to a card is a KB bug: fix the card.

### Scoring

| Score | Meaning |
|---|---|
| 2 | All key facts present and correct |
| 1 | Main fact correct, some key facts missing |
| 0 | Wrong, or main fact missing |
| H | Flag (in addition to the score): stated something that does not exist |

Categories: **L** lookup, **F** flow, **I** invariant/gotcha, **C** change-guidance,
**T** trap (premise is false or commonly confused), **G** gap probe (not covered by cards;
checks the generated tables and that the KB doesn't hurt).

### Results template

| ID | Run | Score | H | Tool calls | Time | Notes |
|---|---|---|---|---|---|---|
| Q01 | baseline | | | | | |
| Q01 | kb | | | | | |

---

## Questions

### Q01 (L) Server handlers for an object update

**Ask:** Which server functions handle a client object update RPC, and which handle the copy sent
to the other replicas?

**Key facts:**
- `DAOS_OBJ_RPC_UPDATE` -> `src/object/srv_obj.c::ds_obj_rw_handler` on the leader shard.
- Forwarded as `DAOS_OBJ_RPC_TGT_UPDATE` -> `src/object/srv_obj.c::ds_obj_tgt_update_handler`.

**KB source:** generated/rpcs.md, flows/write-path.md

### Q02 (L) dRPC pool create

**Ask:** Which engine function handles the dRPC pool-create request, and which Go function sends it?

**Key facts:**
- Method `DRPC_METHOD_MGMT_POOL_CREATE` (207), dispatched in `src/mgmt/srv.c`.
- Engine: `src/mgmt/srv_drpc.c::ds_mgmt_drpc_pool_create`.
- Go: `src/control/server/mgmt_pool.go::mgmtSvc.poolCreate` via `harness.CallDrpc` with
  `daos.MethodPoolCreate`.

**KB source:** generated/drpc.md, flows/pool-create.md

### Q03 (F) Replicated write fan-out

**Ask:** For an object with 3-way replication, how many update RPCs does the client send per
redundancy group, and how do the other replicas get the data?

**Key facts:**
- One RPC to the DTX leader shard (server-side dispatch, `ort_srv_disp`, set in
  `src/object/cli_obj.c`).
- Leader runs `src/dtx/dtx_common.c::dtx_leader_exec_ops` with
  `src/object/srv_obj.c::obj_tgt_update`; remote shards via
  `src/object/srv_obj_remote.c::ds_obj_remote_update`, local via `obj_local_rw()`.

**KB source:** flows/write-path.md

### Q04 (I) When is a DTX committed?

**Ask:** When a client gets a successful reply to a replicated update, is the distributed
transaction already committed on all replicas?

**Key facts:**
- Not necessarily. `src/dtx/dtx_common.c::dtx_leader_end` marks it committable and usually adds it
  to the CoS cache (`dtx_cos_add()`); it is committed later in batches by
  `src/dtx/dtx_common.c::dtx_batched_commit` or piggybacked on later RPCs.
- Synchronous commit only when `dth_sync` is set.

**KB source:** modules/dtx.md

### Q05 (I) -DER_INPROGRESS

**Ask:** A fetch returns `-DER_INPROGRESS`. What does it mean and how is it handled?

**Key facts:**
- The read hit a prepared but not yet committed/aborted DTX.
- The server first tries `dtx_refresh()` to ask the leader; otherwise the client retries.

**KB source:** modules/dtx.md, modules/vos.md

### Q06 (L) Xstream roles

**Ask:** Which engine xstream runs the pool service and rdb, and which runs VOS I/O for a target?

**Key facts:**
- System xstream 0 (`DSS_XS_SYS`): pool/container services, rdb, dRPC handlers.
- One main xstream per target (`DSS_XS_VOS`) for everything touching that target's VOS.
- Defined in `src/include/daos_srv/daos_engine.h::dss_xs_type`.

**KB source:** modules/engine.md, architecture.md

### Q07 (F) Engine startup handshake

**Ask:** After `daos_engine` starts, how does it get its rank and when do modules' `sm_setup`
callbacks run?

**Key facts:**
- Engine calls `src/engine/drpc_client.c::drpc_notify_ready`, then `server_init` waits for
  `DSS_INIT_STATE_SET_UP`.
- `daos_server` (`src/control/server/mgmt_drpc.go::srvModule.handleNotifyReady`) joins the
  system (`mgmtSvc.Join`), then `src/control/server/instance.go::EngineInstance.SetupRank` sends
  `SET_RANK` and `SET_UP`.
- `src/mgmt/srv_drpc.c::ds_mgmt_drpc_set_up` calls `dss_module_setup_all()`.

**KB source:** modules/engine.md, modules/control.md

### Q08 (F) NVMe space during an update

**Ask:** Where is NVMe space allocated for an array update, and when does the allocation become
permanent?

**Key facts:**
- Reserved in `src/vos/vos_io.c::vos_update_begin` -> `src/vos/vos_io.c::vos_reserve_blocks` ->
  `src/vea/vea_api.c::vea_reserve`.
- Published in `src/vos/vos_io.c::vos_update_end` inside the VOS transaction; an error passed to
  `vos_update_end()` cancels the reservation.

**KB source:** flows/write-path.md, modules/vos.md

### Q09 (T) Rebuild op codes

**Ask:** What does the `RB_OP_DRAIN` rebuild operation do differently from `RB_OP_REINT`?

**Key facts:**
- Neither exists. `src/include/daos_srv/rebuild.h::daos_rebuild_opc_t` has `RB_OP_REBUILD`,
  `RB_OP_RECLAIM`, `RB_OP_FAIL_RECLAIM`, `RB_OP_UPGRADE`.
- Drain, reintegrate, exclude and extend all use `RB_OP_REBUILD`; target states in the pool map
  (`DRAIN`, `UP`, `DOWN`, `NEW`) drive the difference.

**KB source:** modules/rebuild.md

### Q10 (F) Engine death to exclusion

**Ask:** When an engine dies, which code in the pool service turns that into excluding its
targets?

**Key facts:**
- `src/pool/srv_pool.c::ds_pool_crt_event_cb` -> `queue_event()` ->
  `src/pool/srv_pool.c::events_handler` -> `src/pool/srv_pool.c::handle_event` ->
  `src/pool/srv_pool.c::pool_svc_exclude_ranks` -> `pool_svc_update_map()` with `MUS_SWIM`.
- Only if pool self-heal has `DAOS_SELF_HEAL_AUTO_EXCLUDE` (and the system self-heal allows it).

**KB source:** flows/rebuild.md

### Q11 (I) Rebuild not starting

**Ask:** A target was excluded but no rebuild started. List the reasons this can happen.

**Key facts (any 3 for score 2):**
- Environment `DAOS_REBUILD=no`.
- System self-heal property disables pool rebuild for automatic events.
- `src/include/daos_srv/pool.h::is_pool_rebuild_allowed` false: rebuild stopped by admin, or
  automatic event without `AUTO_REBUILD`/`DELAY_REBUILD` in pool self-heal.
- `DAOS_SELF_HEAL_DELAY_REBUILD`: scheduled but deferred until an admin starts it.
- Reintegration mode `DAOS_REINT_MODE_NO_DATA_SYNC`.

**KB source:** flows/rebuild.md

### Q12 (F) After rebuild

**Ask:** What happens to the pool map and the data once a rebuild after an exclude succeeds?

**Key facts:**
- `src/pool/srv_pool.c::ds_pool_tgt_finish_rebuild`: `DOWN` -> `DOWNOUT`.
- `src/rebuild/srv.c::rebuild_task_complete_schedule` queues `RB_OP_RECLAIM` to remove data that no
  longer belongs on each target.

**KB source:** flows/rebuild.md, modules/rebuild.md

### Q13 (F) Stale pool map on the client

**Ask:** How does a client that holds an old pool map find out during I/O and recover?

**Key facts:**
- Server `obj_ioc_begin()` compares the RPC's map version and returns `-DER_STALE`.
- Client completion/retry (`src/object/cli_obj.c::obj_retry_cb`) refreshes the pool map and
  resends.

**KB source:** modules/object.md, flows/write-path.md

### Q14 (F) Initial pool metadata

**Ask:** During pool create, which function writes the initial pool map and properties, and where
are they stored?

**Key facts:**
- `src/pool/srv_pool.c::ds_pool_create_handler` on the new pool-service leader, inside an rdb
  transaction: `src/pool/srv_pool.c::init_pool_metadata` and `ds_cont_init_metadata()`, then
  `rdb_tx_commit()`.
- Stored in the pool service's rdb (Raft-replicated, backed by VOS).

**KB source:** flows/pool-create.md

### Q15 (T) Starting pool service replicas

**Ask:** Which dRPC call does the engine use to start the pool service replicas on other ranks?

**Key facts:**
- None: it is a CaRT collective RPC, not dRPC. `src/rsvc/srv.c::ds_rsvc_dist_start` broadcasts
  `RSVC_START`; each replica runs `src/rsvc/srv.c::ds_rsvc_start_handler` ->
  `src/rsvc/srv.c::ds_rsvc_start` -> `src/rdb/rdb.c::rdb_create`.

**KB source:** flows/pool-create.md

### Q16 (C) Engine module loading

**Ask:** How does `daos_engine` load a server module, and what must a new module provide?

**Key facts:**
- `src/engine/module.c::dss_module_load` dlopens `lib<name>.so` and looks up `<name>_module`
  (`struct dss_module`).
- Default list `src/engine/init.c::MODULE_LIST`; aliases in `src/engine/init.c::modules_load`;
  check mode uses `MODS_LIST_CHK`.

**KB source:** modules/engine.md

### Q17 (C) Adding a dRPC method

**Ask:** I need a new dRPC call from `daos_server` to the engine. Which files must change?

**Key facts:**
- Method ID in `src/include/daos/drpc_modules.h`.
- Go constant in `src/control/lib/daos/drpc.go`.
- Protobuf in `src/proto` (regenerate Go and C code).
- Engine dispatch in `src/mgmt/srv.c` and handler in `src/mgmt/srv_drpc.c`.
- Go caller (e.g. in `src/control/server/mgmt_*.go`).

**KB source:** modules/control.md, .github/instructions/control.instructions.md

### Q18 (C) Changing a VOS persistent struct

**Ask:** I want to add a field to `struct vos_obj_df`. What else must I do?

**Key facts:**
- It is on-media format (`src/vos/vos_layout.h`): bump the layout/pool version and handle
  upgrade of existing pools.

**KB source:** modules/vos.md

### Q19 (I) VOS transactions

**Ask:** Can I call a function that may yield the ULT while inside a VOS/umem transaction?

**Key facts:**
- No. Never yield inside a `umem` transaction (`vos_tx_begin()` ... `vos_tx_end()`).

**KB source:** modules/vos.md, .github/copilot-instructions.md

### Q20 (L) MS leadership and state

**Ask:** In `daos_server`, how does a management-service handler make sure it runs on the
leader, and how is MS state persisted?

**Key facts:**
- `src/control/server/mgmt_svc.go::mgmtSvc.checkLeaderRequest` ->
  `src/control/system/raft/database.go::Database.CheckLeader`.
- State changes are Raft log entries: `src/control/system/raft/raft.go::Database.submitRaftUpdate`,
  applied by `src/control/system/raft/raft.go::fsm.Apply` (bbolt-backed).

**KB source:** modules/control.md

### Q21 (L) EC parity marker

**Ask:** How are EC parity extents distinguished from data extents?

**Key facts:**
- High bit of the offset: `src/include/daos/object.h::DAOS_EC_PARITY_BIT` (`1ULL << 63`).

**KB source:** modules/object.md

### Q22 (I) Rebuild throttling

**Ask:** How does the engine stop rebuild traffic from starving client I/O?

**Key facts:**
- Rebuild scan and migrate RPCs are classified as `SCHED_REQ_MIGRATE`
  (`src/rebuild/srv.c::rebuild_get_req_attr`, `src/object/srv_mod.c`), and the per-xstream
  scheduler (`src/engine/sched.c`) throttles by request type.

**KB source:** modules/rebuild.md, modules/engine.md

### Q23 (T) DFS write path

**Ask:** Which DAOS KV API call does `dfs_write()` use to store file data?

**Key facts:**
- None: `src/client/dfs/io.c::dfs_write` calls `daos_array_write()` (array API), which becomes
  `DAOS_OPC_OBJ_UPDATE` sub-tasks (`src/client/array/dc_array.c::dc_array_io`).

**KB source:** flows/write-path.md

### Q24 (G) Container open handler

**Ask:** Which server function handles `CONT_OPEN`?

**Key facts:**
- `src/container/srv_container.c::ds_cont_op_handler` (shared by most container ops).

**KB source:** generated/rpcs.md only (no container card)

### Q25 (G) Raft vote handling in rdb

**Ask:** Which function handles Raft vote requests between pool service replicas?

**Key facts:**
- `RDB_REQUESTVOTE` -> `src/rdb/rdb_raft.c::rdb_requestvote_handler`.

**KB source:** generated/rpcs.md only (no rdb card)

### Q26 (G) Aggregation ULTs

**Ask:** Which functions start and run VOS and EC aggregation for a container on a target?

**Key facts:**
- `src/container/srv_target.c::cont_start_agg` creates `src/container/srv_target.c::cont_agg_ult`
  and `src/container/srv_target.c::cont_ec_agg_ult` with `sched_create_ult()`.
- VOS aggregation calls `src/vos/vos_aggregate.c::vos_aggregate`.

**KB source:** modules/vos.md (partial; no container card)
