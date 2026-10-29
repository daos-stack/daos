---
id: control
kind: module
layer: control-plane (Go)
depends_on: [engine, mgmt, proto, security]
used_by: [dmg, daos_agent, daos_server]
related: [drpc, flow-pool-create, engine]
---
# Control plane (`src/control`, Go)

Overview: [src/control/README.md](../../../src/control/README.md),
[src/control/server/README.md](../../../src/control/server/README.md),
[src/control/drpc/README.md](../../../src/control/drpc/README.md),
[src/control/lib/control/README.md](../../../src/control/lib/control/README.md).
Protobufs: `src/proto` (Go stubs in `src/control/common/proto`). Never edit
`src/control/vendor`.

## Purpose

All management of a DAOS system: parse config, prepare/format SCM and NVMe, start and supervise
`daos_engine` processes, run the replicated **management service (MS)** (system membership, pool
service records, system-wide operations) and expose it over gRPC to `dmg`, `daos_agent` and
other servers. The control plane talks to its local engines over dRPC; it never touches pool
data directly.

## Binaries (`src/control/cmd`)

| Binary | Role |
|---|---|
| `src/control/cmd/daos_server` | Server daemon: config, storage, engine supervision, gRPC server, MS replica |
| `src/control/cmd/daos_agent` | Client-node daemon: dRPC server for libdaos (attach info, credential signing), caches system info |
| `src/control/cmd/dmg` | Admin CLI (go-flags commands -> `lib/control` calls) |
| `src/control/cmd/daos` | User CLI; uses libdaos through cgo |
| `src/control/cmd/daos_server_helper` | Privileged helper for storage operations (via `src/control/pbin`) |
| `src/control/cmd/ddb` | VOS debug tool front end |

## Package map

| Package | Role |
|---|---|
| `src/control/server` | `daos_server` core: startup (`server.go`), MS (`mgmt_*.go`), ctl service (`ctl_*.go`), engine harness/instances, dRPC modules for engine upcalls |
| `src/control/server/config` | `daos_server.yml` model and validation |
| `src/control/server/engine` | Engine config and process runner |
| `src/control/server/storage` | SCM/NVMe/metadata providers and formatting |
| `src/control/system` | Membership, members, pool service records, fault domains |
| `src/control/system/raft` | MS Raft database (hashicorp raft + bbolt): members, pools, system props |
| `src/control/lib/control` | Client API used by `dmg`/agent: request/response types, `InvokeUnaryRPC` |
| `src/control/lib/daos` | cgo bindings for DAOS constants (status codes, dRPC method IDs) |
| `src/control/drpc` | dRPC client/server, `Module` interface |
| `src/control/security` | Certificates, credentials, ACLs, gRPC authorization |
| `src/control/events` | RAS events and pub/sub, forwarding to the MS leader |
| `src/control/common/proto` | Generated protobuf Go code (`ctl`, `mgmt`, `srv`, `shared`, ...) |
| `src/control/lib/hardware`, `src/control/lib/ranklist`, `src/control/lib/spdk` | Topology/fabric discovery, rank sets, SPDK bindings |

## Startup of `daos_server`

1. `src/control/server/server.go::Start` -> `newServer` -> `createServices` (MS + ctl services),
   `addEngines` (one `EngineInstance` per configured engine), `setupGrpc`, `start`.
2. `src/control/server/harness.go::EngineHarness.Start` runs each instance
   (`src/control/server/instance_exec.go::EngineInstance.Run`): wait for storage
   (format via `src/control/server/ctl_storage_rpc.go::ControlService.StorageFormat` if needed),
   launch `daos_engine` with `src/control/server/engine/exec.go::Runner.Start`.
3. Engine reports ready over dRPC: `DRPC_METHOD_SRV_NOTIFY_READY` ->
   `src/control/server/mgmt_drpc.go::srvModule.handleNotifyReady` ->
   `src/control/server/instance_drpc.go::EngineInstance.NotifyDrpcReady`.
4. `src/control/server/instance_exec.go::EngineInstance.finishStartup` -> `handleReady`:
   `determineRank` joins the system through gRPC
   `src/control/server/mgmt_system.go::mgmtSvc.Join` on the MS leader, then
   `src/control/server/instance.go::EngineInstance.SetupRank` sends dRPC `SET_RANK` and
   `SET_UP` (engine side: [engine.md](engine.md)).

## Management service (MS)

- `src/control/server/mgmt_svc.go::mgmtSvc` implements gRPC `MgmtSvc` (`src/proto/mgmt/mgmt.proto`).
  Only MS replicas (configured by `mgmt_svc_replicas` in `daos_server.yml`) run Raft; requests
  that need the leader are checked with `src/control/server/mgmt_svc.go::mgmtSvc.checkLeaderRequest`
  (`src/control/system/raft/database.go::Database.CheckLeader`).
- State lives in `src/control/system/raft/database.go::Database` and is changed only by
  submitting Raft log entries (`src/control/system/raft/raft.go::Database.submitRaftUpdate`,
  applied in `src/control/system/raft/raft.go::fsm.Apply`).
- Membership: `src/control/system/membership.go::Membership`; pool service records and states:
  `src/control/system/pool.go`.
- Pool ops in `src/control/server/mgmt_pool.go` (`PoolCreate`, `PoolExclude`, `PoolDrain`,
  `PoolReintegrate`, `PoolExtend`, `PoolRebuildStart`, ...) resolve the pool, then forward to an
  engine with `src/control/server/harness.go::EngineHarness.CallDrpc`. See
  [flows/pool-create.md](../flows/pool-create.md) and [generated/drpc.md](../generated/drpc.md).
- System ops in `src/control/server/mgmt_system.go` (`SystemQuery`, `SystemStop`,
  `SystemStart`, `SystemExclude`, `SystemRebuildManage`, `GetAttachInfo`, `ClusterEvent`).

## gRPC services

| Service | Proto | Server files | Scope |
|---|---|---|---|
| `CtlSvc` | `src/proto/ctl` | `src/control/server/ctl_*.go` (`ControlService`) | Per-host: storage/network scan, storage format, SMD, ranks |
| `MgmtSvc` | `src/proto/mgmt` | `src/control/server/mgmt_*.go` (`mgmtSvc`) | System-wide via MS leader: pools, containers, system, checker |

Client side: `src/control/lib/control/rpc.go::Client.InvokeUnaryRPC` sends a request either to a
list of hosts (ctl) or to the MS leader (mgmt; retried on leader changes). Multi-host responses
embed `src/control/lib/control/response.go::HostErrorsResp`; MS responses are unpacked with
`src/control/lib/control/response.go::convertMSResponse`.

## dRPC

- Go side of the protocol: `src/control/drpc` (`ClientConnection`, `DomainSocketServer`,
  `Module`). Method IDs come from C via `src/control/lib/daos/drpc.go`.
- Server -> engine: `EngineHarness.CallDrpc` -> `src/control/server/instance_drpc.go::EngineInstance.CallDrpc`.
- Engine -> server: `src/control/server/mgmt_drpc.go::srvModule` (notify ready, cluster events,
  pool service lookups), checker upcalls in `src/control/server/mgmt_drpc_checker.go`.
- libdaos -> agent: `src/control/cmd/daos_agent/mgmt_rpc.go::mgmtModule.handleGetAttachInfo`
  (served from `src/control/cmd/daos_agent/infocache.go::InfoCache`) and
  `src/control/cmd/daos_agent/security_rpc.go::SecurityModule` (signed credentials).

## Invariants and gotchas

- MS state changes must go through Raft (`Database` methods), never mutate in-memory copies.
- Leader-only RPCs must call `checkLeaderRequest`; clients rely on `lib/control` to find the leader.
- dRPC method/proto changes touch C and Go together: `src/include/daos/drpc_modules.h`,
  `src/control/lib/daos/drpc.go`, `src/proto/*`, the engine handler in `src/mgmt`. Regenerate
  protobuf code rather than editing `*.pb.go` / `*.pb-c.c`.
- Privileged operations go through `daos_server_helper` (`src/control/pbin`), not directly.
- Config examples: `utils/config/daos_server.yml`, `utils/config/daos_agent.yml`,
  `utils/config/daos_control.yml`.

## Tests

- Unit: `*_test.go` next to the code; mocks in files such as `src/control/server/mocks.go`
  and `src/control/system/raft/mocks.go`. Run `src/control/run_go_tests.sh`.
- Functional: `src/tests/ftest/control` and the rest of `src/tests/ftest`.

## Where to look for...

| Question | Look at |
|---|---|
| A `dmg` command's implementation | `src/control/cmd/dmg/<area>.go` -> `src/control/lib/control/<area>.go` -> `src/control/server/mgmt_<area>.go` |
| What happens when an engine dies | `src/control/server/instance_exec.go::EngineInstance.handleExit`, `src/control/events` |
| How a rank joins | `src/control/server/mgmt_system.go::mgmtSvc.Join` |
| Storage format | `src/control/server/ctl_storage_rpc.go::ControlService.StorageFormat`, `src/control/server/storage` |
| Server config fields | `src/control/server/config/server.go::Server` |
