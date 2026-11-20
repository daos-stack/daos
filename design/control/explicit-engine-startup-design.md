# High-Level Design: Explicit Engine Startup

## 1. Summary

Prior to version 3.0, `daos_server` launched its engines as soon as their
storage was formatted and again on every restart of the process.
`dmg storage format` returned before any engine had joined. While this flow
is convenient for development scenarios, it makes a production DAOS system
harder to set up and manage because an admin or orchestration solution has
to poll the system and interpret the result to determine whether or not the
system has completed initialization and is ready to use.

This design makes engine launch an explicit step. The storage format step is
only responsible for formatting storage, and the system waits before moving
on to the initialization step. `dmg system initialize` starts the engines
and returns with a clear system status overview when they have joined;
`dmg system extend` is used to add new servers to a running system. On
restart, a server now asks the management service (MS) whether each engine
should launch.

## 2. Background

The server launched engines without consulting the MS, so nothing between
format and launch could tell a server whether the system wanted an engine
running. `dmg storage format` had no completion signal, so scripts slept and
polled `dmg system query`.

## 3. Goals and Non-Goals

### Goals

1. Format does not launch engines. A command that starts them returns with a
   per-host result once they have joined or failed.
2. No schema change to the MS database. Existing systems upgrade in place.
3. The pre-3.0 one-step format remains available
   (`dmg storage format --init`).

### Non-Goals

- Server registration. Between format and initialize, `dmg system query` has
  no members to show.
- Changing what `dmg system stop` does. A stopped rank relaunches when its
  `daos_server` restarts, and SWIM moves it to Excluded while it is down, as
  before. A durable stop is a follow-on; see Section 8.

## 4. Design

### 4.1 Member states

A new state, `MemberStateAwaitStart`, reports an engine that is formatted
and held, waiting for an explicit start signal. It is an engine state on the
server, never a member state in the MS. A held engine reports itself to the
MS as Stopped, so the member's record, which preflight decides from, is left
as it was.

The rule the rest of the design follows: an engine never launches before an
admin command has started it once. Format launches nothing. Initialize and
extend say run. After that, a server restart relaunches its member engines as
it always has.

### 4.2 Launch gate

The engine's run loop gains a hold between format and launch. The superblock
is readable there, and an MS replica's database runs without engines, so
the MS can answer for every engine in the system before any of them launch.

The hold is armed when `daos_server` starts and after every format. A
preflight verdict of start or an explicit start releases it:
`dmg system initialize`, `extend` or `start`, or `dmg storage format
--init` or `--replace`. Engine restarts within a running `daos_server` do
not pass through it.

### 4.3 Preflight

When the hold is armed, the server asks the MS whether each engine should
start. The MS is the only authority on whether a rank belongs to the system;
the server keeps no record of its own.

The MS answers per engine:

| Condition | Verdict |
|---|---|
| rank not a member | hold: awaiting `dmg system initialize` or `extend` |
| member state is AdminExcluded | hold: awaiting `dmg system clear-exclude` |
| otherwise | start |

Holding an AdminExcluded rank at launch replaces the relaunch, refused join
and kill that its server repeated on every restart.

The server retries a request that times out, since the MS may still be
electing a leader. Any other error leaves the engine held until an
administrator starts it.

### 4.4 Initialize and extend

`dmg system initialize` and `dmg system extend` start the engines on a
hostlist, wait for them to join, and report one result per joined rank and
one per host that fell short.

A hostlist is the only way to reach a host with no ranks: the MS knows nothing
about a server until an engine joins, and `dmg system start` fans out to member
ranks, of which a freshly formatted host has none. Initialize cannot be that
command once the system is in use. Its contract is that it returns when the
system is ready to use. On a system that already has pools a rerun is a
stale bringup script, so it refuses; until the first pool exists it can be
rerun to pick up hosts a first run missed. Extend is the command for a live
system: it refuses a system with no members, and the ranks it adds hold no
pool data until `dmg pool extend` places some on them.

Neither command has a default timeout; how long bringup may take is the
orchestrator's call (`--timeout`).

An empty membership on the leader is now the normal state between format
and initialize, not a sign of a replica behind on its log, so
`dmg system query` reports it rather than redirecting to another replica.

### 4.5 Server startup ordering

Prior to this change, the engine restart manager and the Prometheus
exporter started after every engine had reported ready once. An engine may
now never start, so both start with the server, and per-engine metric
sources register from each engine's ready callback.

## 5. Compatibility and Interoperability

- No MS database schema change.
- Four new gRPC methods: `MgmtSvc.EnginePreflight`, `SystemInitialize`,
  `SystemExtend`; `CtlSvc.StartEngines`. `StorageFormatReq` gains `start`.
- Upgrade: stop the system, upgrade, restart `daos_server`. Member ranks
  relaunch as before; nothing waits for an initialize.
- Every bringup script must follow `dmg storage format` with
  `dmg system initialize`, or pass `--init`.

## 6. External Interfaces

- `dmg storage format [--init]`
- `dmg system initialize [-l <hosts>] [--timeout <duration>]`
- `dmg system extend [-l <hosts>] [--timeout <duration>]`
- `dmg system query` prints a hint when the system has no members.
- Admin guide: *Engine Startup* in `docs/admin/administration.md`,
  *System Initialization* in `docs/admin/deployment.md`.

## 7. Testing and Validation

`control/system_startup.py` covers initialize being rerunnable until a pool
exists, extend adding a host, and a killed server's rank rejoining;
`control/storage_format_init.py` covers the one-step format. The ftest
harness runs initialize or extend after format; NLT runs initialize, or start
on reused storage.

## 8. Risks and Future Work

- Durable stop: a rank stopped with `dmg system stop` staying stopped across
  a `daos_server` restart, so that a planned outage is stop, reboot the nodes
  at any pace, start. It was built on this branch and removed before landing:
  with the stopped rank kept in the group map, collective RPCs that touch it
  wait out their timeouts for as long as it is down; with it removed from the
  map at the stop, a full restart excludes ranks that have not rejoined yet.
  The open question is how a stopped rank should look to the group map and to
  its pools, and it needs its own design.
- Checker mode at launch: the preflight response is the place for the MS to
  tell every launch whether to come up in check mode, replacing the launch,
  join, kill and relaunch sequence the checker uses today.
