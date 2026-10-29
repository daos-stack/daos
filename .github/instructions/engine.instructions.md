---
applyTo: "src/engine/**,src/include/daos_srv/daos_engine.h,src/include/daos_srv/iv.h"
description: "daos_engine runtime (xstreams, ULTs, scheduler, module loading, dRPC server) context and rules"
---
# Working in the engine runtime

Read [docs/kb/modules/engine.md](../../docs/kb/modules/engine.md) first; module/RPC tables are in
[docs/kb/generated/modules.md](../../docs/kb/generated/modules.md) and
[docs/kb/generated/drpc.md](../../docs/kb/generated/drpc.md).

- Everything runs in Argobots ULTs: never block an xstream (no `sleep()`, blocking syscalls,
  pthread waits); use `dss_sleep()`, ABT primitives, `dss_ult_execute()` or `dss_chore_register()`.
- Respect xstream roles: VOS work on the target main xstream, pool/rdb/dRPC work on xs 0.
- Startup ordering matters: modules are initialised in `dss_module_init_all()` before xstreams
  exist; `sm_setup` runs later from `dss_module_setup_all()` after the rank is set up.
- Changes to `struct dss_module` or scheduler request types affect every server module.
- Tests: `src/engine/tests`.
