---
applyTo: "src/object/**,src/include/daos_srv/object.h,src/include/daos/object.h"
description: "Object layer (client and server I/O) context and rules"
---
# Working in the object layer

Read [docs/kb/modules/object.md](../../docs/kb/modules/object.md) first; end-to-end I/O is in
[docs/kb/flows/write-path.md](../../docs/kb/flows/write-path.md); opcode -> handler table in
[docs/kb/generated/rpcs.md](../../docs/kb/generated/rpcs.md).

- `cli_*.c` run in libdaos (client tasks on the TSE scheduler); `srv_*.c` run in `daos_engine`
  on target xstreams. Don't mix assumptions between the two.
- Server handlers must validate the request with `obj_ioc_begin()` (handles, capabilities, pool
  map version) and return `-DER_STALE` for old maps; the client refreshes and retries.
- Modifications go through DTX (`dtx_leader_begin()` on the leader shard,
  `ds_obj_tgt_update_handler()` on others). Don't write to VOS outside a DTX handle.
- RPC formats: `src/object/obj_rpc.h` (`DAOS_ISEQ_*`/`DAOS_OSEQ_*` + `CRT_RPC_DECLARE`). Any wire
  change requires bumping `DAOS_OBJ_VERSION` and keeping the previous format working.
- Tests: `src/object/tests`, `src/tests/suite/daos_obj.c`, `src/tests/suite/daos_obj_ec.c`.
