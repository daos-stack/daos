# High-Level Design: Explicit Engine Startup

Prior to version 3.0, `daos_server` launched its engines as soon as their
storage was formatted and again on every restart of the process.
`dmg storage format` returned before any engine had joined, so an admin or
orchestrator had to poll the system and interpret the result to tell whether
it was ready to use.

This design makes engine launch a step of its own, with a result:

```bash
$ dmg storage format -l <hosts>      # formats storage; launches nothing
$ dmg system initialize -l <hosts>   # starts the engines; returns once they have joined
```

The rule the rest of the design follows: an engine never launches before an
admin command has started it once. After that, a server restart relaunches
its member engines as it always has.

## Launch gate

The engine's run loop gains a hold between format and launch. The superblock
is readable there, and an MS replica's database runs without engines, so the
MS can answer for every engine in the system before any of them launch.

The hold is armed when `daos_server` starts and after every format. Engine
restarts within a running `daos_server` do not pass through it.

## Preflight

When an engine reaches the hold, its server asks the MS whether the engine
should start (`MgmtSvc.EnginePreflight`). The MS is the only authority on
whether a rank belongs to the system; the server keeps no record of its own.

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

## Explicit start

A held engine that preflight does not start waits for a command:

- `dmg system initialize` or `dmg system extend`, for engines that have never
  joined.
- `dmg system start`, for a held member rank, such as one whose exclusion was
  just cleared.
- `dmg storage format --replace`, which starts the engine it formats so that
  the replaced rank rejoins without a further command.

Initialize and extend start the engines on a hostlist
(`CtlSvc.StartEngines`), wait for them to join, and report one result per
joined rank and one per host that fell short. They take a hostlist because
the MS knows nothing about a server until one of its engines joins:
`dmg system start` fans out to member ranks, and a freshly formatted host has
none.

The two commands differ only in the system they accept:

- The contract of `dmg system initialize` is that it returns when the system
  is ready to use. A system that already has pools is in use, so a rerun is
  refused; until the first pool exists it can be rerun to pick up hosts a
  first run missed.
- `dmg system extend` is the command for a live system, and refuses one with
  no members. The ranks it adds hold no pool data until `dmg pool extend`
  places some on them.

Neither has a default timeout; how long bringup may take is the
orchestrator's call (`--timeout`).

## What else had to change

- The engine restart manager and the Prometheus exporter started once every
  engine had reported ready. An engine may now never start, so both start
  with the server, and per-engine metric sources register from each engine's
  ready callback.
- An empty membership on the MS leader is now the normal state between
  format and initialize, not a sign of a replica behind on its log, so
  `dmg system query` reports it, with a pointer to `dmg system initialize`,
  rather than redirecting to another replica.
- An engine that exits with status 0 without having been asked to stop is
  now recorded as a failure and raises an engine-died event, so that a start
  waiting on it reports the exit instead of waiting for a ready notification
  that will not come.

## Compatibility

- Every bringup script must follow `dmg storage format` with
  `dmg system initialize`.
- An upgraded system needs no initialize: its ranks are members, so
  preflight starts them.
- No schema change to the MS database. Four new gRPC methods:
  `MgmtSvc.EnginePreflight`, `SystemInitialize` and `SystemExtend`, and
  `CtlSvc.StartEngines`.

## Not in this change

- `dmg system stop` is unchanged: a stopped rank relaunches when its
  `daos_server` restarts. A stop that survives the restart, so that a planned
  outage is stop, reboot the nodes at any pace, start, needs its own design.
- Checker mode at launch. The preflight response is the place for the MS to
  tell every launch whether to come up in check mode, replacing the launch,
  join, kill and relaunch sequence the checker uses today.

## Testing

`control/system_startup.py` covers initialize being rerunnable until a pool
exists, extend adding a host, and a killed server's rank rejoining. The ftest
and NLT harnesses run initialize after format (extend, for servers added to
a running system), so the functional tests bring their systems up this way.
