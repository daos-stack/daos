---
applyTo: "src/control/**,src/proto/**"
description: "Go control plane (daos_server, daos_agent, dmg) context and rules"
---
# Working in the control plane

Read [docs/kb/modules/control.md](../../docs/kb/modules/control.md) first; dRPC method mapping is
in [docs/kb/generated/drpc.md](../../docs/kb/generated/drpc.md) and an end-to-end example in
[docs/kb/flows/pool-create.md](../../docs/kb/flows/pool-create.md).

- Never edit `src/control/vendor` or generated protobuf code (`*.pb.go`, `*.pb-c.[ch]`);
  change `src/proto` and regenerate.
- MS state changes go through Raft (`system/raft.Database`); leader-only handlers call
  `checkLeaderRequest`.
- A new dRPC method needs matching changes in `src/include/daos/drpc_modules.h`,
  `src/control/lib/daos/drpc.go`, the Go caller and the engine handler (`src/mgmt/srv.c`),
  then `utils/kb/kb.py extract`.
- Follow existing patterns: `dmg` command -> `lib/control` function -> `server/mgmt_*.go`
  handler; add `_test.go` coverage with the existing mocks.
- Tests: `src/control/run_go_tests.sh`.
