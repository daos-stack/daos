# High-Level Design: Explicit Engine Startup

## 1. Summary

Prior to version 3.0, `daos_server` launched its engines as soon as their
storage was formatted and again on every restart of the process.
`dmg storage format` returned before any engine had joined, and a rank
stopped with `dmg system stop` came back the next time `daos_server`
restarted. While this flow is convenient for development scenarios, it makes
a production DAOS system harder to set up and manage because an admin or
orchestration solution has to poll the system and interpret the result to
determine whether or not the system has completed initialization and is
ready to use.

This design makes engine launch an explicit step. The storage format step is
only responsible for formatting storage, and the system waits before moving
on to the initialization step. `dmg system initialize` starts the engines
and returns with a clear system status overview when they have joined;
`dmg system extend` is used to add new servers to a running system. On
restart, a server now asks the management service (MS) whether each engine
should launch, and a rank stopped with `dmg system stop` stays stopped until
it is explicitly restarted by the admin using `dmg system start`.

## 2. Background

The server launched engines without consulting the MS, and the MS did not
treat Stopped as an administrator decision. A stopped rank relaunched when
its `daos_server` restarted, and while it stayed down a SWIM dead event
moved it to Excluded. The only way to keep a rank down across a reboot was
`dmg system exclude`, which also drops it from every pool.

## 3. Goals and Non-Goals

### Goals

1. Format does not launch engines. A command that starts them returns with a
   per-host result once they have joined or failed.
2. A rank stopped with `dmg system stop` stays stopped across a `daos_server`
   restart. Any other member rank restarts with its server.
3. No schema change to the MS database. Existing systems upgrade in place.
4. The pre-3.0 one-step format remains available
   (`dmg storage format --start`).

### Non-Goals

- Server registration. Between format and initialize, `dmg system query` has
  no members to show.
- Changing what `dmg system stop` does to a running engine.

## 4. Design

### 4.1 Member states

A new state, `MemberStateAwaitStart`, reports an engine that is formatted
and held, waiting for an explicit start signal. It is an engine state on the
server, never a member state in the MS. A held engine reports itself to the
MS as Stopped, so the member's record, which preflight decides from, is left
as the administrator set it.

The rule behind the state changes is that an engine runs because an admin
command said so, and the last such command survives a `daos_server`
restart. Format launches nothing. Initialize, extend and start say run.
Stop says stop. Preflight is the server asking the MS what the last command
was. So `dmg system stop` takes running ranks down and they stay down,
across restarts of their servers, until `dmg system start`.

This is what a planned outage with reboots needs. Engines relaunch as each
node's `daos_server` comes back, so without a durable stop the first nodes
up bring up pool services while the rest are still down, and any node more
than SWIM's suspect timeout behind is excluded and rebuilt around. With it
the sequence is `dmg system stop`, reboot the nodes at any pace,
`dmg system start`.

Stopped sits between the two excluded states:

| State | Set by | In group map | Relaunches on boot | Returns to service by |
|---|---|---|---|---|
| Excluded | SWIM dead event | no | yes | rejoins when its engine restarts |
| Stopped | `dmg system stop` | yes | no | `dmg system start` |
| AdminExcluded | `dmg system exclude` | no | no | `dmg system clear-exclude`, then a restart |

AdminExcluded takes the rank out of every pool and keeps it out until
`clear-exclude`. An engine crash is recorded as Errored, so a crashed rank
never lands in Stopped.

A stopped rank stays in the group map. A pool service treats a rank missing
from the map as dead when it steps up, with no grace period
(`pool_svc_check_node_status`), and a full stop and start must not exclude
ranks that are still coming up.

The same rule applied to a subset of ranks has two consequences. A rank
stopped for node maintenance needs `dmg system start` after the node is
back, where it used to rejoin on its own. And its pools still exclude it
under their self-heal policy once SWIM reports it dead, as they do today;
pool services do not consult the MS. Section 8 covers closing that gap.

### 4.2 Launch gate

The engine's run loop gains a hold between format and launch. The superblock
is readable there, and an MS replica's database runs without engines, so
the MS can answer for every engine in the system before any of them launch.

The hold is armed when `daos_server` starts and after every format. A
preflight verdict of start or an explicit start releases it:
`dmg system initialize`, `extend` or `start`, or `dmg storage format
--start` or `--replace`. Engine restarts within a running `daos_server` do
not pass through it.

### 4.3 Preflight

When the hold is armed, the server asks the MS whether each engine should
start. The MS is the only authority on administrator intent; the server
keeps no record of its own.

The MS answers per engine:

| Condition | Verdict |
|---|---|
| rank not a member | hold: awaiting `dmg system initialize` or `extend` |
| member state is Stopped or AdminExcluded | hold: awaiting `dmg system start` |
| otherwise | start |

The server retries a request that times out, since the MS may still be
electing a leader. Any other error leaves the engine held until an
administrator starts it.

### 4.4 Initialize and extend

`dmg system initialize` and `dmg system extend` start the engines on a
hostlist, wait for them to join, and report one result per joined rank and
one per host that fell short.

A hostlist is the only way to reach a host with no ranks; `dmg system start`
addresses member ranks. Initialize's contract is that it returns when the
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
- Upgrade: `dmg system stop`, upgrade, restart `daos_server`,
  `dmg system start`. A server restarted without a stop relaunches its
  engines, as before.
- Every bringup script must follow `dmg storage format` with
  `dmg system initialize`, or pass `--start`.

## 6. External Interfaces

- `dmg storage format [--start]`
- `dmg system initialize [-l <hosts>] [--timeout <duration>]`
- `dmg system extend [-l <hosts>] [--timeout <duration>]`
- `dmg system query` prints a hint when the system has no members.
- Admin guide: *Engine Startup* in `docs/admin/administration.md`,
  *System Initialization* in `docs/admin/deployment.md`.

## 7. Testing and Validation

`control/system_startup.py` covers the behaviors above and
`control/storage_format_start.py` the one-step format. The ftest harness
runs initialize or extend after format; NLT runs initialize, or start on
reused storage.

Tests that stopped a rank to simulate a failure now kill its engine, since
a stopped rank no longer comes back with its server.

## 8. Risks and Future Work

- A partially stopped rank is excluded from its pools by self-heal while
  the MS reports it Stopped. The follow-on is to carry the stopped ranks in
  the group update so pool services skip exclusion for them, leaving a rank
  to lose its pool membership only when it dies unstopped or the admin
  excludes it.
- Checker mode at launch: the preflight response is the place for the MS to
  tell every launch whether to come up in check mode, replacing the launch,
  join, kill and relaunch sequence the checker uses today.
