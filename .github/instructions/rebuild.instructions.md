---
applyTo: "src/rebuild/**,src/include/daos_srv/rebuild.h,src/object/srv_obj_migrate.c"
description: "Rebuild and object migration context and rules"
---
# Working in rebuild / migration

Read [docs/kb/modules/rebuild.md](../../docs/kb/modules/rebuild.md) first; the end-to-end trace
(failure -> exclude -> rebuild -> reclaim) is [docs/kb/flows/rebuild.md](../../docs/kb/flows/rebuild.md);
placement and target states are covered in [docs/kb/modules/pool.md](../../docs/kb/modules/pool.md).

- Rebuild is leader-driven (pool service leader, xs 0) and executed on every engine; scan and
  migration run on target xstreams. Keep that split.
- All map-driven movement uses `RB_OP_REBUILD`; behaviour differs by target state in the pool
  map, not by op code. Successful rebuilds are followed by `RB_OP_RECLAIM`.
- Scanning must wait for DTX resync; migration must fetch at stable epochs.
- Progress is reported through rebuild IV; never block the leader waiting on targets.
- Tests: `src/tests/suite/daos_rebuild.c` and friends, `src/tests/ftest/rebuild`.
