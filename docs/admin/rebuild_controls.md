# Detailed Pool Operations: Interactive Rebuild Controls

## Introduction

Although the self-healing policy can control whether a rebuild will occur,
occasionally an administrator may need to stop a rebuild already started and
restart it later with a `rebuild start` command (or, perform an alternate
action/rebuild such as direct reintegration). For this purpose, DAOS provides
the following interactive rebuild control command-line interfaces:

* `dmg pool <label> rebuild stop [--force]`
* `dmg pool <label> rebuild start`
* `dmg system rebuild stop [--force]`
* `dmg system rebuild start`

The system-level commands (e.g., `dmg system rebuild stop`) apply to all pools in the DAOS
system (i.e., they have the same effect as if multiple `dmg pool rebuild stop` commands are issued,
one per pool).

Upon stopping a pool's rebuild, its rebuild state as reported by `dmg pool query`
will be an idle state, and an error status=`-2027` (`-DER_OP_CANCELED` DAOS error code).

The effect of a `rebuild stop` command is "one shot", meaning only a pool's
currently-running rebuild is stopped and there is no persistent effect on future
operations. Subsequent self-healing automatic recovery, or administrator command
(e.g., system stop, system/pool exclude, reintegrate, drain, pool extend) can
trigger new rebuild(s). Also, if a rebuild is stopped, whatever progress it had
made in reconstructing the data in the pool is not retained — a subsequent
`rebuild start` command will start the rebuild from the beginning (i.e., this is
not a pause/resume interface).

Some circumstances where rebuild stop/start controls may be helpful are:

* When a pool has insufficient free space to accommodate relocation of affected
  data upon engine(s) excluded/drained. For example, if a rebuild fails with
  `status=-1007` (`-DER_NOSPACE`) (that will likely repeat in its automatic
  retries). Stopping such a rebuild allows an administrator to perform alternate
  actions (e.g., directly reintegrate the lost engine(s); and/or pool expansion
  to more engines).
* A system with many tens of pools are all rebuilding simultaneously (requiring
  substantial CPU and network resources in the DAOS system). And the
  administrator deciding in such cases to stagger an overall recovery by
  manually commanding pools to rebuild in smaller batches.
* A long-running rebuild might be stuck (e.g., due to any unforeseen DAOS bug).


## Rebuild Phases

An important detail of the DAOS rebuild design/implementation is that it
proceeds in two phases: the rebuild itself, and a following "reclaim" phase to
clean up space that rebuild used while it was operating.

For a **successful rebuild**, the sequence is:

1. Run `op:Rebuild`
2. Run `op:Reclaim` to clean up

For a **failed rebuild**, the sequence is:

1. Run `op:Rebuild`
2. Run `op:Fail_reclaim` to clean up
    * If `Fail_reclaim` *itself* failed, retry `Fail_reclaim`
    * If `Fail_reclaim` succeeded, retry the original `op:Rebuild`

A `rebuild stop` command (without `--force`) will immediately stop a rebuild in
the `op:Rebuild` phase. During `op:Reclaim`, the command is rejected with a
`-DER_BUSY` error (the rebuild has already succeeded). During `op:Fail_reclaim`,
the command succeeds (no error returned to the administrator), but the
`Fail_reclaim` cleanup is allowed to finish — once it completes, the normal
retry of the original `op:Rebuild` is suppressed, and the rebuild transitions to
the stopped state. The `--force` option to `rebuild stop` is intended for the
pathological case where `Fail_reclaim` itself is repeatedly failing and retrying
in a loop; `--force` will terminate the `Fail_reclaim` immediately (provided it
has already failed at least once).

The `dmg pool query` command can be used to monitor rebuild state transitions
after issuing a `rebuild stop`. See the section
[Rebuild Stop Command Responses](#rebuild-stop-command-responses) for details on
the responses returned in different circumstances.


## Example Usage: Stop a Single Pool Rebuild, then Direct Reintegration

A system has detected the loss of an engine (rank 3) that has 8 storage targets.
A corresponding rebuild has launched on pool `p1` after the engine's exclusion
from the pool map. The administrator decides to stop this rebuild
(perhaps because it is possible to quickly remedy the rank 3 engine issue, restart it, 
and reintegrate it into the system and pool). This will result in a single
rebuild for the reintegration, rather than two rebuilds (for the initial exclusion,
and later for the reintegration).

**1.** Observe the pool `p1` is rebuilding after the fault is detected.

The pool state reflects 8 disabled targets (`disabled=8`) corresponding to the exclusion
of engine rank 3. Rebuild is underway (`busy` rebuild state).
```bash
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=8, leader=6, version=77, state=TargetsExcluded
Pool health info:
- Disabled ranks: 3
- Rebuild busy, 1 objs, 524288000 recs
- Data redundancy: degraded
```

**2.** Stop the rebuild and monitor with multiple `pool query` commands until the stop is confirmed
(`stopped, state=idle, status=-2027`). Notice some rebuild state changes while waiting.

Using a command at single-pool scope, no output is expected if the request is successfully
sent to the storage system.
```bash
$ dmg pool rebuild stop p1
```

Run `dmg pool query` a few times, though not too frequently. The `--health-only` option is
useful to keep the overall query lighter-weight (by interacting with the pool service leader
engine, and that engine not interacting with all pool engines while it is handling the query).

Rebuild state output `stopping` along with `state=busy` and `status=-2027` indicates
the stop command is being processed, and rebuild has not been entirely stopped yet.
```bash
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=8, leader=6, version=77, state=TargetsExcluded
Pool health info:
- Disabled ranks: 3
- Rebuild stopping (state=busy, status=-2027)
```

Rebuild state (temporarily) transitioned to `busy` without an error status indicates the
`op:Rebuild` is no longer running, and a reclaim phase is now running (in this case, `op:Fail_reclaim`, since a stopped rebuild is processed in the same way as a failed rebuild).
```bash
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=8, leader=6, version=77, state=TargetsExcluded
Pool health info:
- Disabled ranks: 3
- Rebuild busy, 0 objs, 0 recs
```

Rebuild state `idle` with `status=-2027` indicates the rebuild is now stopped.
```bash
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=8, leader=6, version=77, state=TargetsExcluded
Pool health info:
- Disabled ranks: 3
- Rebuild stopped (state=idle, status=-2027)
- Data redundancy: degraded
```

**3.** Restart engine rank 3, wait for it to join the system, directly reintegrate it
back into pool `p1`, and wait for the rebuild to finish successfully.

```bash
$ dmg system start --ranks=3
# Repeat dmg system query commands until engine rank 3 shown in the joined state
$ dmg system query
Rank      State
----      -----
[0-2,4-7] Joined
3         Stopped

$ dmg system query
Rank  State
----  -----
[0-7] Joined

# Now, reintegrate engine rank 3 into pool p1
$ dmg pool reintegrate --ranks=3 p1
```

Run `dmg pool query` a few times, possibly with the `--health-only` option, and not too
frequently.

First, it may be seen that the pool map has been updated for the reintegrating engine rank 3
(pool map `version=85` instead of 77, and targets `disabled=0` instead of 8). And it could be
that the pool rebuild has not yet started, with state reflecting the same state following
the previous (stopped) rebuild (`state=idle, status=-2027`).
```bash
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=0, leader=6, version=85, state=Ready
Pool health info:
- Rebuild stopped (state=idle, status=-2027)
- Data redundancy: normal
```

Rebuild is now `busy` (performing the reintegration) according to rebuild state.
```bash
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=0, leader=6, version=85, state=Ready
Pool health info:
- Rebuild busy, 1 objs, 455081984 recs
- Data redundancy: normal
```

Rebuild is still `busy`, though object / record counts have been reset. Also, the pool map version
has increased to 93 (previously 85). This indicates the `op:Rebuild` has completed, and
the rebuild is cleaning up in `op:Reclaim` phase. The pool map was updated to promote the
engine rank 3 targets from `UP` (during reintegration) to `UP_IN` (reintegration complete,
ready for client I/O).
```bash
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=0, leader=6, version=93, state=Ready
Pool health info:
- Rebuild busy, 0 objs, 0 recs
- Data redundancy: normal
```

Rebuild is still `busy` in `op:Reclaim` phase.
```bash
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=0, leader=6, version=93, state=Ready
Pool health info:
- Rebuild busy, 1 objs, 0 recs
- Data redundancy: normal
```

Rebuild (including reclaim) has finished since state is `done`. Engine rank 3 has been reintegrated
into the pool.
```bash
# all done
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=0, leader=6, version=93, state=Ready
Pool health info:
- Rebuild done, 1 objs, 0 recs
- Data redundancy: normal
```


## Example Usage: Stop Multiple Pools Rebuilding

Consider a DAOS system with multiple pools, labeled `p1`, `p2`, etc. A
`daos_engine` suffers a fault and the system detection of the engine loss
results in pool map updates and all pools rebuilding. The administrator would
prefer to stop the exclusion rebuilds, repair/restart the engine, and directly
reintegrate the engine into the system and all pools.

**1.** Observe the engine is excluded from the system, and pools begin
rebuilding after the fault is detected.

```bash
$ dmg system query
Rank      State
----      -----
[0-2,4-7] Joined
3         Stopped
```

The stopped engine will eventually also be reported as `Excluded` from the system
```bash
$ dmg system query
Rank      State
----      -----
[0-2,4-7] Joined
3         Excluded
```

Attempting to stop pool rebuilds too soon (before they have actually started) will produce an error.
The `dmg system rebuild stop` command reports how many pools had the request successfully
issued (in this case, 0 successful pools).
```bash
$ dmg system rebuild stop
System-rebuild stop request succeeded on 0 pools
ERROR: dmg: system rebuild stop failed: pool-rebuild stop failed on pool p1: pool-rebuild stop failed: DER_NONEXIST(-1005): The specified entity does not exist, pool-rebuild stop failed on pool p2: pool-rebuild stop failed: DER_NONEXIST(-1005): The specified entity does not exist
```

The following output shows that pool `p2` has excluded targets and rebuild has started:
- `State` column shows `TargetsExcluded`
- `Disabled` column reports 8 out of 64 targets are disabled, corresponding here to the lost
engine rank 3 targets.
- `Rebuild State` column shows `busy`

Also, pool `p1` has not excluded targets yet, and has not started rebuild:
- `State` column is `Ready`
- `Disabled` column reflects 0 targets disabled.
- `Rebuild State` column is `done`, reflecting state from a previously-completed rebuild.
```bash
$ dmg system list-pools -v
Label UUID                                 State           SvcReps   SCM Size SCM Used SCM Imbalance NVME Size NVME Used NVME Imbalance Disabled UpgradeNeeded? Rebuild State
----- ----                                 -----           -------   -------- -------- ------------- --------- --------- -------------- -------- -------------- -------------
p1    cdf27ec1-ed97-4aa6-a766-39a2ed2136a1 Ready           [0-1,4-6] 105 GB   37 GB    28%           0 B       0 B       0%             0/64     None           done
p2    dcfe63e0-e10f-464d-b18c-0915e52e048c TargetsExcluded [0-2,6-7] 49 GB    4.6 GB   62%           0 B       0 B       0%             8/64     None           busy
```

Shortly after this moment, both pools p1 and p2 should show `Rebuild State` =  `busy`.

**2.** Stop all pool rebuilds with one command, and confirm they have stopped.

The successful `system rebuild stop` command will confirm that it succeeded for all (in this case 2)
pools.
```bash
$ dmg system rebuild stop
System-rebuild stop request succeeded on 2 pools
```

When the rebuilds have been stopped, the `Rebuild State` is expected to be `idle` (seen in both
`dmg system list-pools` and `dmg pool query` command output). And the rebuild status will show
error status `-2027` (`-DER_OP_CANCELED` DAOS error code)

```bash
$ dmg system list-pools -v
Label UUID                                 State           SvcReps   SCM Size SCM Used SCM Imbalance NVME Size NVME Used NVME Imbalance Disabled UpgradeNeeded? Rebuild State
----- ----                                 -----           -------   -------- -------- ------------- --------- --------- -------------- -------- -------------- -------------
p1    cdf27ec1-ed97-4aa6-a766-39a2ed2136a1 TargetsExcluded [0-1,4-6] 105 GB   37 GB    28%           0 B       0 B       0%             8/64     None           idle
p2    dcfe63e0-e10f-464d-b18c-0915e52e048c TargetsExcluded [0-2,6-7] 49 GB    4.6 GB   62%           0 B       0 B       0%             8/64     None           idle

$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=8, leader=6, version=102, state=TargetsExcluded
Pool health info:
- Disabled ranks: 3
- Rebuild stopped (state=idle, status=-2027)
- Data redundancy: degraded

$ dmg pool query --health-only p2
Pool dcfe63e0-e10f-464d-b18c-0915e52e048c, ntarget=64, disabled=8, leader=7, version=46, state=TargetsExcluded
Pool health info:
- Disabled ranks: 3
- Rebuild stopped (state=idle, status=-2027)
- Data redundancy: degraded
```

**3.** Restart the affected engine, reintegrate engine and monitor rebuilds
until finished.

```bash
$ dmg system start --ranks=3
# Repeat dmg system query commands until engine rank 3 shown in the joined state
$ dmg system query
Rank      State
----      -----
[0-2,4-7] Joined
3         Stopped

$ dmg system query
Rank  State
----  -----
[0-7] Joined

$ dmg system reintegrate --ranks=3
```

Use `dmg system list-pools` or `dmg pool query` = (with short delays between commands) to
monitor the reintegration rebuilds on the pools.

Both pools have had their target states updated due to the reintegration command, and corresponding
reintegration rebuilds started, based on the `dmg system list-pools` command output:
- `Disabled` column reports 0 disabled targets (was 8 targets disabled previously)
- `Rebuild State` column indicates `busy` for both pools.

```bash
$ dmg system list-pools -v
Label UUID                                 State SvcReps   SCM Size SCM Used SCM Imbalance NVME Size NVME Used NVME Imbalance Disabled UpgradeNeeded? Rebuild State
----- ----                                 ----- -------   -------- -------- ------------- --------- --------- -------------- -------- -------------- -------------
p1    cdf27ec1-ed97-4aa6-a766-39a2ed2136a1 Ready [0-1,4-6] 120 GB   42 GB    28%           0 B       0 B       0%             0/64     None           busy
p2    dcfe63e0-e10f-464d-b18c-0915e52e048c Ready [0-2,6-7] 56 GB    4.9 GB   62%           0 B       0 B       0%             0/64     None           busy
```

Wait for rebuilds to complete (i.e., for `Rebuild State` to report `done` for both pools):
```bash
$ dmg system list-pools -v
Label UUID                                 State SvcReps   SCM Size SCM Used SCM Imbalance NVME Size NVME Used NVME Imbalance Disabled UpgradeNeeded? Rebuild State
----- ----                                 ----- -------   -------- -------- ------------- --------- --------- -------------- -------- -------------- -------------
p1    cdf27ec1-ed97-4aa6-a766-39a2ed2136a1 Ready [0-1,4-6] 120 GB   42 GB    31%           0 B       0 B       0%             0/64     None           done
p2    dcfe63e0-e10f-464d-b18c-0915e52e048c Ready [0-2,6-7] 56 GB    4.9 GB   62%           0 B       0 B       0%             0/64     None           done
```

## Example Usage: Stop and Restart a Rebuild Failing in Fail\_reclaim

For some cases in which rebuild fails, `Fail_reclaim` runs successfully, and the process
repeats, an ordinary `rebuild stop` command (without `--force` option) is sufficient. However,
if the `Fail_reclaim` itself fails, and retries itself repeatedly, eventually the administrator
may need to terminate this bad loop with the `--force` option.

```bash
# Administrator has been monitoring pool rebuild state with dmg pool query and noticed occasional
# rebuild failures, and retries - with busy and failed states seen in a cycle.

# Attempt to stop the rebuild
$ dmg pool rebuild stop <pool_label>

# Continue to monitor with dmg pool query, though the pool rebuild continues to run
# and the `stopped` state is never seen. Observe more rebuild busy / failed cycles.

# Forcibly stop the rebuild (since it may be looping in `Fail_reclaim`)
$ dmg pool rebuild stop --force <pool_label>
```



## Rebuild Stop Command Responses

The `rebuild stop` command behavior depends on the current rebuild phase.
The following subsections describe the responses in each case.

### No Rebuild Currently Running

When no rebuild is currently running, the command will report a "nonexist"
error:
```bash
# system-level command applying to two pools, both of which have not started rebuilding yet
$ dmg system rebuild stop
System-rebuild stop request succeeded on 0 pools
ERROR: dmg: system rebuild stop failed: pool-rebuild stop failed on pool p1: pool-rebuild stop failed: DER_NONEXIST(-1005): The specified entity does not exist, pool-rebuild stop failed on pool p2: pool-rebuild stop failed: DER_NONEXIST(-1005): The specified entity does not exist

# single pool scope command applied to one pool that is not currently rebuilding
$ dmg pool rebuild stop p2
ERROR: dmg: pool-rebuild stop failed: DER_NONEXIST(-1005): The specified entity does not exist
```

### Rebuild Finished Successfully, Reclaim in Progress

When the rebuild stage has successfully finished and is in its `op:Reclaim` cleanup
stage, `dmg` will report a busy error:
```bash
$ dmg system rebuild stop
System-rebuild stop request succeeded on 0 pools
ERROR: dmg: system rebuild stop failed: pool-rebuild stop failed on pool p1: pool-rebuild stop failed: DER_BUSY(-1012): Device or resource busy, pool-rebuild stop failed on pool p2: pool-rebuild stop failed: DER_BUSY(-1012): Device or resource busy
```

### Rebuild Failed, Fail\_reclaim in Progress

When the rebuild has failed and is in its `op:Fail_reclaim` cleanup stage,
`rebuild stop` (without `--force`) succeeds with **no error**. The
`Fail_reclaim` is allowed to run to completion, and once it finishes, the
normal retry of the original `op:Rebuild` is suppressed. The rebuild then
transitions to the stopped state (`state=idle, status=-2027`).

```bash
$ dmg pool rebuild stop <pool_label>
# No error output — command accepted, stop deferred until Fail_reclaim finishes

# Monitor with pool query until the rebuild reaches the stopped state
$ dmg pool query --health-only <pool_label>
# ... Rebuild busy (Fail_reclaim still running) ...

$ dmg pool query --health-only <pool_label>
# ... Rebuild stopped (state=idle, status=-2027) ...
```

If the `Fail_reclaim` itself is repeatedly failing and retrying (never reaching
a successful completion), the `--force` option can be used to terminate it
immediately:

```bash
$ dmg pool rebuild stop --force <pool_label>
```

Note: `--force` only takes effect if the `Fail_reclaim` has already failed at
least once. On the first attempt (before any failure), `--force` behaves the
same as a non-force stop (deferring until `Fail_reclaim` finishes).

### Another Rebuild Queued for the Same Pool

If another rebuild task is already queued for the same pool (e.g., a
reintegration was scheduled after the current rebuild), the `rebuild stop`
command will return a no-permission error:
```bash
$ dmg pool rebuild stop <pool_label>
ERROR: dmg: pool-rebuild stop failed: DER_NO_PERM(-1001): No permission
```
