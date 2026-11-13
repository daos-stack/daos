---
id: engine
kind: module
layer: server/runtime
depends_on: [cart, gurt, bio, vos, chk, common]
used_by: [bio, chk, container, dtx, mgmt, object, pipeline, pool, rdb, rebuild, rsvc, security, vos]
related: [xstream, ult, iv, drpc, control]
---
# Engine runtime (`src/engine`)

Overview: [src/engine/README.md](../../../src/engine/README.md). Public server API:
`src/include/daos_srv/daos_engine.h`; IV API: `src/include/daos_srv/iv.h`. Internal:
`src/engine/srv_internal.h`.

## Purpose

`daos_engine` is the data-plane process (one per rank). `src/engine` is its runtime, not a
storage module: it parses arguments, initialises gurt/CaRT/Argobots/SPDK, loads the server
modules (`libpool.so`, `libobj.so`, ...), creates the xstreams and per-xstream CaRT contexts,
dispatches incoming RPCs to module handlers through a scheduler, hosts the dRPC server that
`daos_server` talks to, and provides the ULT, collective and IV helpers every module uses.

## File map

| File | Role |
|---|---|
| `src/engine/init.c` | `main`, argument parsing, `server_init` / `server_fini`, init-state machine |
| `src/engine/module.c` | Module loading (`dss_module_load`), `dss_module_init_all`, `dss_module_setup_all`, handler registration |
| `src/engine/srv.c` | Xstream creation (`dss_srv_init`, `dss_start_one_xstream`), per-xstream progress, `dss_rpc_hdlr`, `dss_sleep` |
| `src/engine/ult.c` | ULT helpers: `dss_ult_create`, `dss_ult_create_all`, `dss_ult_execute`, collectives, chores, `dss_offload_exec` |
| `src/engine/sched.c` | Per-xstream scheduler: request queues, throttling, space pressure, watchdog |
| `src/engine/server_iv.c` | IV framework on top of CaRT IV (`ds_iv_*`) |
| `src/engine/drpc_listener.c`, `src/engine/drpc_progress.c`, `src/engine/drpc_handler.c` | dRPC server: socket listener, poll loop, dispatch by dRPC module ID |
| `src/engine/drpc_client.c` | Engine -> `daos_server` dRPC calls (`dss_drpc_call`, `drpc_notify_ready`) |
| `src/engine/drpc_ras.c` | RAS events to the control plane (`ds_notify_ras_event`) |
| `src/engine/drpc_chk.c` | Checker upcalls to the control plane |
| `src/engine/srv_cli.c`, `src/engine/rpc.c`, `src/engine/util.c`, `src/engine/profile.c`, `src/engine/srv_metrics.c` | Server-side client helpers, RPC utils, misc, profiling, metrics |

## Startup and shutdown

1. `src/engine/init.c::main` -> `src/engine/init.c::server_init`:
   TLS keys, logging, topology (`dss_topo_init()`), telemetry, `drpc_init()`, Argobots,
   `crt_init_opt()`, placement init, `modules_load()` (dlopen each module),
   `bio_nvme_init()`, `src/engine/module.c::dss_module_init_all` (each `sm_init`, register CaRT
   protocols and dRPC handlers), `src/engine/srv.c::dss_srv_init` (xstreams + contexts).
2. `src/engine/drpc_client.c::drpc_notify_ready` tells `daos_server` the engine is up
   (`DRPC_METHOD_SRV_NOTIFY_READY`), then `server_init` waits for `DSS_INIT_STATE_SET_UP`.
3. After the rank joins the system, `daos_server` sends `DRPC_METHOD_MGMT_SET_RANK` and
   `DRPC_METHOD_MGMT_SET_UP`; `src/mgmt/srv_drpc.c::ds_mgmt_drpc_set_up` runs
   `dss_module_setup_all()` (each module's `sm_setup`, e.g. start pool services) and sets the
   init state. See [control.md](control.md) for the Go side.
4. Shutdown: `src/engine/init.c::server_fini` reverses the order (module cleanup, xstreams,
   unload, CaRT, Argobots).

## Xstreams and threading

`src/include/daos_srv/daos_engine.h::dss_xs_type` selects the target of `dss_ult_create()`:

| Xstream | Count | Runs |
|---|---|---|
| System xs 0 (`DSS_XS_SYS`) | 1 | Pool/container services, rdb/Raft, rebuild leader, dRPC handlers, mgmt |
| SWIM (`DSS_XS_SWIM`), dRPC listener (`DSS_XS_DRPC`) | 1 each | Failure detection, dRPC socket |
| Target main (`DSS_XS_VOS`) | `dss_tgt_nr` | Everything that touches the target's VOS: I/O handlers, aggregation, DTX, rebuild scan/migrate |
| Helper / offload (`DSS_XS_IOFW`, `DSS_XS_OFFLOAD`) | `dss_tgt_offload_xs_nr` | I/O forwarding (chores), EC/checksum/compression offload |

- Xstream id math: `src/engine/srv_internal.h::DSS_MAIN_XS_ID`,
  `src/engine/srv_internal.h::DSS_MAIN_XS_ID_WITH_HELPER_POOL` (helpers either per target or a
  shared pool when there are fewer helpers than targets).
- Current context: `dss_get_module_info()` -> `dmi_xs_id`, `dmi_tgt_id`, `dmi_ctx`.
- Per-module per-xstream TLS: `sm_key` in `struct dss_module` + `dss_module_key_get()`.

## Entry points

- ULTs: `src/engine/ult.c::dss_ult_create` (xs type + target), `dss_ult_create_all()` (one per
  target), `dss_ult_execute()` (run on another xstream and wait), `dss_ult_periodic()`.
- Collectives across targets: `src/engine/ult.c::dss_thread_collective`,
  `src/engine/ult.c::dss_task_collective` and `*_reduce` variants; pool-aware wrapper
  `ds_pool_thread_collective()`.
- Offload: `src/engine/ult.c::dss_chore_register` (queued on helper xstream),
  `src/engine/ult.c::dss_offload_exec`.
- Scheduler: `src/engine/sched.c::sched_create_ult` (tracked background ULTs, e.g. aggregation,
  GC), `sched_req_yield()`, `sched_req_sleep()`, `sched_req_space_check()`; request types
  `SCHED_REQ_UPDATE`, `SCHED_REQ_FETCH`, `SCHED_REQ_GC`, `SCHED_REQ_SCRUB`, `SCHED_REQ_MIGRATE`.
- RPC dispatch: `src/engine/srv.c::dss_rpc_hdlr` (registered with CaRT for every context) asks
  the module for request attributes and enqueues the handler with `sched_req_enqueue()`.
  Handlers/protocols come from `struct dss_module` (`sm_handlers`, `sm_proto_fmt`), list in
  [generated/modules.md](../generated/modules.md).
- dRPC server: `src/engine/drpc_handler.c::drpc_hdlr_process_msg` routes by dRPC module ID to
  `sm_drpc_handlers`; method table in [generated/drpc.md](../generated/drpc.md).
- IV: `src/engine/server_iv.c::ds_iv_class_register`, `src/engine/server_iv.c::ds_iv_ns_create`,
  `src/engine/server_iv.c::ds_iv_fetch`, `src/engine/server_iv.c::ds_iv_update`.

## Key structures

- `src/include/daos_srv/daos_engine.h::dss_module`: module descriptor (`sm_init`, `sm_fini`,
  `sm_setup`, `sm_cleanup`, `sm_key`, `sm_handlers`, `sm_drpc_handlers`, `sm_metrics`).
- `src/include/daos_srv/daos_engine.h::dss_module_info`: per-xstream context.
- `src/engine/srv_internal.h::dss_xstream`: xstream state (ABT pool, scheduler, chore queue).
- `src/include/daos_srv/daos_engine.h::dss_chore`: offloaded unit of work.

## Invariants and gotchas

- Never block an xstream: no `sleep()`, blocking syscalls, pthread mutex/cond waits. Use
  `dss_sleep()`, ABT primitives, or offload.
- Never yield inside a `umem`/VOS transaction.
- Deep call chains (VOS iteration, migration) need a big stack: pass `DSS_DEEP_STACK_SZ`.
- Work on a target's VOS must run on that target's main xstream; pool/rdb services run on xs 0.
- Modules are shared libraries loaded at runtime: `dss_module_load()` dlopens `lib<name>.so` and
  looks up the symbol `<name>_module`. The default list is
  `src/engine/init.c::MODULE_LIST` (`-m` overrides; aliases such as `vos` -> `vos_srv` are
  mapped in `src/engine/init.c::modules_load`). Check mode (`-C`) uses `MODS_LIST_CHK`;
  `pipeline` is appended only when `DAOS_PIPELINE` is set.

## Tests

- `src/engine/tests` (dRPC listener/handler/progress, Argobots stack/perf).
- Engine behaviour is mostly covered by module tests and `src/tests/ftest/server`.

## Where to look for...

| Question | Look at |
|---|---|
| Why an RPC runs on a given xstream | `src/engine/srv.c::dss_rpc_hdlr`, module `dms_get_req_attr` callbacks |
| Engine command-line options | `src/engine/init.c::parse` |
| How IO is throttled / space pressure | `src/engine/sched.c` |
| How the engine reports to `daos_server` | `src/engine/drpc_client.c`, `src/engine/drpc_ras.c` |
