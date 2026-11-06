---
applyTo: "src/dtx/**,src/include/daos_srv/dtx_srv.h,src/include/daos/dtx.h"
description: "DTX (distributed transaction) context and rules"
---
# Working in DTX

Read [docs/kb/modules/dtx.md](../../docs/kb/modules/dtx.md) first; the leader/non-leader sequence
is in [docs/kb/flows/write-path.md](../../docs/kb/flows/write-path.md).

- Leader: `dtx_leader_begin()` -> `dtx_leader_exec_ops()` -> `dtx_leader_end()`.
  Non-leader: `dtx_begin()` -> local work -> `dtx_end()`.
- Commit is normally asynchronous (CoS cache + `dtx_batched_commit` ULT); only `dth_sync` commits
  inline. Do not assume a DTX is committed when the client gets its reply.
- `-DER_INPROGRESS` means a reader hit a prepared DTX: refresh (`dtx_refresh()`) or retry; never
  treat it as a hard failure.
- Resends must be detected (`dtx_handle_resend()`) so updates are never applied twice.
- Persistent DTX state lives in VOS (`src/vos/vos_dtx.c`); keep the two in sync when changing
  states (`src/include/daos/dtx.h`).
- Unit tests: `src/dtx/tests`; integration: `src/tests/suite/daos_dist_tx.c`.
