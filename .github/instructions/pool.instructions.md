---
applyTo: "src/pool/**,src/include/daos_srv/pool.h,src/include/daos/pool.h,src/include/daos/pool_map.h,src/common/pool_map.c"
description: "Pool, pool service and pool map context and rules"
---
# Working in the pool module

Read [docs/kb/modules/pool.md](../../docs/kb/modules/pool.md) first; creation end-to-end is in
[docs/kb/flows/pool-create.md](../../docs/kb/flows/pool-create.md); opcode -> handler table in
[docs/kb/generated/rpcs.md](../../docs/kb/generated/rpcs.md).

- Pool-service code (`src/pool/srv_pool.c`) runs on the system xstream of replica ranks, only does
  work as leader, and changes metadata only inside an rdb transaction on the current term.
- Per-target state is `ds_pool_child`; target work must run on the target xstream (use collective
  helpers), never inline on xs 0.
- Pool map versions are monotonic; every map change must be distributed (IV) and may need a
  rebuild (`ds_rebuild_schedule()`).
- rdb layout changes need a version bump in `src/pool/srv_layout.h` and upgrade handling.
- Tests: `src/tests/suite/daos_pool.c`, `src/tests/suite/daos_rebuild.c`, `src/tests/ftest/pool`.
