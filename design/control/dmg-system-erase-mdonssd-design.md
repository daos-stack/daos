# High-Level Design: `dmg system erase` in MD-on-SSD Mode

## 1. Summary

`dmg system erase` destroys all DAOS pools and unconfigures per-rank storage
(engine superblocks and, in MD-on-SSD mode, the `control_metadata`
directory), bringing every rank into `AwaitFormat` state so the system can be
formatted again from scratch via `dmg storage format`. Because some of the
ranks being erased are also Management Service (MS) raft replicas, the
operation has to coordinate stopping and wiping the system database (sysdb)
alongside wiping engine storage, without ever leaving the system in a state
where no administrator action can recover it.

This design covers the current server-side implementation of that
coordination in `src/control/server/mgmt_system.go`: which host erases what
and in which order, how the MS raft cluster is safely torn down and later
re-formed, and a handful of correctness fixes (fanout rank exclusion, result
address population, and deterministic on-disk durability) required to make
the operation reliable in MD-on-SSD deployments.

## 2. Background

In MD-on-SSD mode, per-engine superblocks and (for MS replica hosts) the
system raft database live under a configurable `control_metadata` directory,
separate from the `data`/`meta`/`wal` tiers used for pool storage. Erasing a
system therefore means, for every rank: stopping the engine, removing its
superblock, and — for the subset of ranks that are also MS replicas —
stopping raft and removing the sysdb files, before the control-plane process
can safely restart into a pristine, unformatted state.

The failure-prone part of this is the MS raft cluster itself. Only one
replica (the *bootstrap* replica — the first entry in `mgmt_svc_replicas`)
is able to form a brand-new single-node raft cluster from scratch; every
other replica must *join* an existing cluster. Naively erasing and
restarting every replica's control-plane process at the same time, with no
ordering guarantees, risks races such as a restarted replica forming or
rejoining a cluster before every replica's sysdb has actually been wiped, or
a crash mid-operation leaving some hosts in a partially-erased state with no
administrator-visible path to recovery.

## 3. Goals and Non-Goals

### Goals

1. Erase all engine storage and the system database on every rank, leaving
   the system in `AwaitFormat` state, ready for `dmg storage format`.
2. Avoid any race where a replica could rejoin a stale or partially-erased
   cluster.
3. Report accurate, complete fanout results back to the `dmg` client (no
   missing host addresses, no masked errors).
4. Make on-disk durability of storage removal deterministic, not
   timing-dependent.

### Non-Goals

- Resetting PMem (SCM) configuration back to Memory Mode (unchanged
  pre-existing behavior, called out separately to the administrator).
- Automatically recovering from a crash that occurs after the leader's own
  raft DB has been stopped (the "point of no return", see Section 8);
  manual recovery is documented instead (Appendix A).
- A client-side blocking wait for `AwaitFormat` across all ranks (see
  Section 8.2, Future Work).

## 4. Design

### 4.1 Roles and Terminology

Two concepts must not be conflated:

| Term | Meaning | How determined |
|---|---|---|
| **MS leader** | The replica currently holding raft leadership | Dynamic; elected by raft, can change at any time (`svc.sysdb.IsLeader()`) |
| **Bootstrap replica** | The replica designated to form a *brand-new* single-node raft cluster when none exists | Static config: the *first* entry in `mgmt_svc_replicas` (`Database.IsBootstrap()`, `system/raft/database.go`) |

These are orthogonal. The replica that happens to be the raft leader at the
moment `dmg system erase` is invoked is **not necessarily** the bootstrap
replica, and in general won't be after the first time the system has been
reformed following an erase.

### 4.2 High-Level Flow

`dmg system erase` is routed by the client to whichever replica is currently
the MS leader (an external request hitting a non-leader replica gets
`*system.ErrNotLeader`, which the client automatically retries against the
leader hint). Once it reaches the leader, three classes of host are handled
differently:

1. **The leader itself** — erases its own raft DB and local engines
   in-process, then restarts (exec) *last*, after coordinating everything
   else.
2. **Other MS replica peers** — each erases its own raft DB and local
   engines (via a forwarded, internal `SystemErase` RPC) and restarts (exec)
   independently, as soon as it has acknowledged the leader's request.
3. **Non-replica engine-only hosts** — handled via a `ResetFormatRanks`
   fanout RPC from the leader; these hosts are not MS replicas, so no
   raft/sysdb handling is needed, only engine superblock/control-metadata
   wiping.

```
dmg system erase
      |
      v
MS leader receives request (SystemErase handler)
      |
      +-- 1. getPeersAndFanout(): resolve peer addrs + build a ResetFormatRanks
      |      fanout request excluding the leader's own rank and all replica
      |      peers' ranks (they handle themselves; see 4.3)
      |
      +-- 2. eraseSysdb(): stop + remove the LEADER's own raft DB files
      |      *** POINT OF NO RETURN: raft is now down on the leader ***
      |
      +-- 3. eraseReplicas(): send an internal SystemErase RPC to each MS
      |      replica peer. Each peer erases its own DB + local engines
      |      synchronously, then acks, then restarts (exec) independently
      |      in the background -- the leader does NOT wait for peers to
      |      finish restarting, only for them to ack that erase is done.
      |
      +-- 4. resetLocalEngines(): stop + wipe the LEADER's own local engines
      |      (superblocks, and control_metadata dir in MD-on-SSD mode)
      |
      +-- 5. wipeEngineSuperblocks(): fan out ResetFormatRanks to all
      |      remaining (non-leader, non-replica) engine ranks
      |
      +-- 6. (deferred) scheduleControlPlaneRestart(): drain gRPC, then
             exec() to restart the leader's own control-plane process --
             this only fires after the handler above returns, i.e. after
             step 5 completes.
```

### 4.3 Non-Leader Replica Path

When the forwarded, internal `SystemErase` RPC lands on a non-leader
replica:

1. `eraseSysdb(errOnFail=true, ...)` — stop and remove this replica's own
   raft DB files.
2. `resetLocalEngines()` — stop local engines, remove superblocks and (in
   MD-on-SSD mode) the `control_metadata` directory.
3. Return success to the leader.
4. (deferred) `scheduleControlPlaneRestart()` fires *after* the response has
   been sent, draining in-flight gRPC calls and then `exec()`-ing to restart
   the control-plane process.

Each replica restarts and re-execs **independently** of the others and of
the leader — there is no synchronization between replicas' restarts, only
between each replica and the leader's single "did you erase yet?" RPC.

### 4.4 Why the Leader Restarts Last

The leader deliberately restarts its own process *last*, after fanning out
`ResetFormatRanks` to all non-replica engine hosts
(`wipeEngineSuperblocks()`). This is **not** because the leader happens to
be the bootstrap replica (it frequently isn't) — it is simply a consequence
of the fact that whichever replica is leader when the erase RPC arrives is
the one responsible for driving the whole operation. It cannot restart until
its own coordinator role is done, i.e. until the fanout to non-MS hosts has
completed, because that fanout logic lives in the same gRPC handler
invocation and `scheduleControlPlaneRestart()` is deferred until the handler
returns.

### 4.5 Post-Erase Raft Cluster Reformation

This is the subtlest part of the design, and worth spelling out precisely:

- **No self-forming cluster among replicas.** Only the one replica flagged
  as the raft bootstrap node (`Database.IsBootstrap()` — the first entry in
  `mgmt_svc_replicas`) will ever form a *new* single-node raft cluster. In
  `bootstrapRaft()` (`system/raft/raft.go`), on restart with a fresh/empty
  raft directory (`newDB == true`), only the bootstrap replica calls
  `BootstrapCluster()`. Non-bootstrap replicas restart with raft idle and no
  cluster to join yet.

- **Nothing even attempts to start until the admin reformats storage.**
  Critically, `sysdb.Start()` itself — on *every* replica, not just the
  bootstrap one — is gated behind that host's `engine.OnStorageReady()`
  callback (`configureFirstEngine()`, `server_utils.go`), which only fires
  once `dmg storage format` runs and calls `NotifyStorageReady()`
  (`ctl_storage_rpc.go`). So no replica even attempts to bootstrap or join a
  cluster until the administrator reformats storage — this is exactly the
  same sequencing as a brand-new deployment, not a special case for erase.

- **The old leader is just an ordinary replica afterwards.** The pre-erase
  leader acting as coordinator during erase was purely a function of it
  being the elected raft leader *at that moment* — unrelated to
  `IsBootstrap()`. Once every replica's raft DB has been wiped and every
  control-plane process has restarted, there is **no leader at all** until
  an administrator reformats storage: no raft cluster exists anywhere in
  this window. Only once storage is reformatted does the bootstrap replica
  form a new single-node cluster and become its leader — not necessarily the
  same host as the pre-erase leader — after which the remaining replicas
  join and are added as raft voters, same as initial cluster formation.

### 4.6 Fanout Rank Exclusion (`getPeersAndFanout`)

The `ResetFormatRanks` fanout request built for non-replica hosts must
exclude the leader's own rank(s) and every replica peer's rank(s), since
those hosts erase and restart themselves (Sections 4.2-4.3) rather than
being reset via RPC fanout. This is done by resolving the leader's own
address (`svc.sysdb.ReplicaAddr()`) and its peers' addresses
(`svc.sysdb.PeerAddrs()`) to a `RankSet` via `Membership.CheckHosts()`, then
deleting those ranks from the pre-built fanout request's rank set.

### 4.7 `MemberResult.Addr` Fix in `rpcFanout()`

`system.Membership.UpdateMemberStates()` is the only place that
opportunistically fills in a fanout result's `Addr` field, and it only runs
when `svc.sysdb.IsLeader()` — which, after the leader has stopped its own
raft DB mid-erase, is no longer true. Since the `dmg` client's
`getResetRankErrors()` hard-errors on any `MemberResult` with an empty
`Addr` ("host address missing for rank N result"), `rpcFanout()` now
independently back-fills any missing `Addr` from the already-fetched
`Membership.HostRanks()` data, regardless of leader state.

### 4.8 Deterministic Fsync Instead of Fixed Sleep

Earlier revisions used `unix.Sync()` followed by a fixed `100ms` sleep to
"give the kernel time" to commit superblock/raft-dir removals before
restarting. This has been replaced with targeted `fsync()` calls on the
specific parent directories whose entries were removed (`fsyncDir()`),
which blocks until that removal is actually durable, rather than guessing a
duration. A bulk `unix.Sync()` call is retained purely as a cheap
best-effort flush for unrelated pending I/O (e.g. from engines that were
just `SIGKILL`ed).

## 5. Compatibility and Interoperability

No on-disk or wire format changes are introduced. The internal,
leader-to-replica `SystemErase` RPC reuses the existing
`mgmt.SystemEraseReq`/`SystemEraseResp` protobuf messages used for the
external, admin-initiated request; the two cases are distinguished at
runtime via the caller's component in the gRPC context
(`build.FromContext(ctx)`), not via a protocol change.

## 6. External Interfaces

- `dmg system erase` — unchanged CLI surface. All ranks must be stopped
  first (`dmg system stop`); this precondition is enforced client-side by
  `checkSystemErase()` in `lib/control/system.go`, shared by both the
  external `dmg` client and the leader's internal replica-forwarding calls.
- The command currently returns as soon as the leader's RPC handler
  returns, which may be before every replica/engine host has finished
  restarting into `AwaitFormat` (see Section 8.2).
- Manual remedial steps for a failed/interrupted erase are documented in
  the admin guide (Appendix A).

## 7. Testing and Validation

### 7.1 Unit Tests

`TestServer_MgmtSvc_SystemErase` and tests for unexported functions therein.

### 7.2 Integration Tests

Manual verification was performed against rebuilt binaries on a multi-host
MD-on-SSD cluster, iterating on `dmg system erase --debug` output and
server-side logs until the leader/replica/engine fanout sequence completed
without error and all ranks reached `AwaitFormat`. Single and multi-replica
permutations were validated.

Automated testing for PMem and MD-on-SSD and single and multi-replica
variations are to be added to the functional test suite as an additional
related change.

## 8. Risks, Mitigations and Future Work

### 8.1 Risks and Mitigations

- Erasing the leader's own raft DB (step 2 in Section 4.2) is an explicit
  **point of no return**: once stopped, the leader cannot safely handle a
  leadership change for the remainder of the operation. If the leader
  process crashes between this point and completion, manual recovery is
  required (Appendix A).

### 8.2 Future Work

`dmg system erase` does not currently wait for all members to reach
`AwaitFormat` before returning (see Section 6). A client-side poll loop in
`cmd/dmg/system.go` to repeatedly call `dmg system query` after a successful
erase is not practical as there is no leader to handle the request but
a simple IsAwaitingFormat gRPC API would be a useful addition to make the
command synchronous.

## Appendix A. Manual Recovery Reference

If `dmg system erase` fails, times out, or is interrupted, administrators
can manually finish bringing the affected hosts to `AwaitFormat` state by
stopping `daos_server`, wiping the `control_metadata` directory (MD-on-SSD
mode) or `wipefs`-ing the PMem devices (DCPM mode), and restarting. This
mirrors the normal "start from scratch" storage reformat procedure and is
documented in full under *System Erase -> Manual Recovery if System Erase
Fails* in `docs/admin/administration.md`.
