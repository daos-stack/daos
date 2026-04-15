# Detailed Pool Operations: Interactive Rebuild Controls

## Introduction

Although the self-healing policy can control whether a rebuild will occur,
occasionally an administrator may need to stop a rebuild already started and
restart it later with a "rebuild start" command (or, perform an alternate
action/rebuild such as direct reintegration). For this purpose, DAOS provides
the following interactive rebuild control command-line interfaces:

* `dmg pool <label> rebuild stop [--force]`
* `dmg pool <label> rebuild start`
* `dmg system rebuild stop [--force]`
* `dmg system rebuild start`

The system commands will apply to all pools in the DAOS system (i.e., have the
same effect as if multiple `pool rebuild stop` commands are issued, one per
pool).

Upon stopping a pool's rebuild, its rebuild state as reported by `dmg pool query`
will be an idle state, and an error status=-2027 (`-DER_OP_CANCELED` DAOS error code).

The effect of a rebuild stop command is "one shot", meaning only a pool's
currently-running rebuild is stopped and there is no persistent effect on future
operations. Subsequent self-healing automatic recovery, or administrator command
(e.g., system stop, system/pool exclude, reintegrate, drain, pool extend) can
trigger new rebuild(s). Also, if a rebuild is stopped, whatever progress it had
made in reconstructing the data in the pool is not retained — a subsequent
"rebuild start" command will start the rebuild from the beginning (i.e., this is
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
    * If `Fail_reclaim` failed, retry `Fail_reclaim`
    * If `Fail_reclaim` succeeded, retry the original `op:Rebuild`

The "rebuild stop" commands are not typically allowed to terminate a rebuild in
the `op:Reclaim` and `op:Fail_reclaim` phases — instead the command must be
issued during the `op:Rebuild` execution. An exception is available with the
`--force` option to "rebuild stop", intended to be applied for rebuilds that
repeatedly fail and possibly may even be looping `Fail_reclaim` operations.

Because of these details, carefully timing the execution of "rebuild stop"
commands is needed, which can be facilitated with pool rebuild state querying
with `dmg pool query`. See the section
[Rebuild Stop Command Errors](#rebuild-stop-command-errors) for examples of
errors returned by "rebuild stop" in different timing circumstances.


## Example Usage: Stop a Single Pool Rebuild, then Direct Reintegration

A system has detected the loss of an engine (rank 3) and has launched a
corresponding rebuild on pool `p1` for that exclusion. The administrator decides
in this case that it is preferable to stop the exclude rebuild rather than let
it finish (perhaps it is possible to quickly remedy the rank 3 engine issue,
restart it, and directly reintegrate it back into the system and pool — rather
than perform 2 separate rebuilds for the exclusion and the reintegration).

**1.** Observe the pool `p1` is rebuilding after the fault is detected.

```bash
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=8, leader=6, version=77, state=TargetsExcluded
Pool health info:
- Disabled ranks: 3
- Rebuild busy, 1 objs, 0 recs
- Data redundancy: degraded
```

**2.** Stop the rebuild and monitor with pool query until the stop is confirmed
("stopped", state=idle, status=-2027). Notice some pool query rebuild state
changes while waiting.

```bash
$ dmg pool rebuild stop p1
```

Run `dmg pool query` in a loop (with short sleeps between commands):

```bash
# confirmation that stop command is being handled
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=8, leader=6, version=77, state=TargetsExcluded
Pool health info:
- Disabled ranks: 3
- Rebuild stopping (state=busy, status=-2027)
```

```bash
# state temporarily changes back to busy, representing the Fail_reclaim stage
# for the stopped operation is now running
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=8, leader=6, version=77, state=TargetsExcluded
Pool health info:
- Disabled ranks: 3
- Rebuild busy, 0 objs, 0 recs
```

```bash
# Fail_reclaim has finished, and now the pool shows its rebuild has fully stopped
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=8, leader=6, version=77, state=TargetsExcluded
Pool health info:
- Disabled ranks: 3
- Rebuild stopped (state=idle, status=-2027)
- Data redundancy: degraded
```

**3.** Directly reintegrate the engine back into pool `p1` and monitor progress
with multiple pool query commands until the reintegration rebuild is
successfully finished.

```bash
$ dmg pool reintegrate --ranks=3 p1
```

Run `dmg pool query` in a loop (with short sleeps between commands):

```bash
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=0, leader=6, version=85, state=Ready
Pool health info:
- Rebuild stopped (state=idle, status=-2027)
- Data redundancy: normal
```

```bash
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=0, leader=6, version=85, state=Ready
Pool health info:
- Rebuild busy, 0 objs, 0 recs
- Data redundancy: normal
```

```bash
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=0, leader=6, version=85, state=Ready
Pool health info:
- Rebuild busy, 1 objs, 455081984 recs
- Data redundancy: normal
```

```bash
# Notice zero objs, recs corresponds to a transition from Rebuild done to Reclaim phase now running.
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=0, leader=6, version=93, state=Ready
Pool health info:
- Rebuild busy, 0 objs, 0 recs
- Data redundancy: normal
```

```bash
$ dmg pool query --health-only p1
Pool cdf27ec1-ed97-4aa6-a766-39a2ed2136a1, ntarget=64, disabled=0, leader=6, version=93, state=Ready
Pool health info:
- Rebuild busy, 1 objs, 0 recs
- Data redundancy: normal
```

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

This shows that pool `p2` has started its rebuild (state "busy"). `p1` is
"done" from a prior rebuild, and will start rebuilding soon.

```bash
$ dmg system list-pools -v
Label UUID                                 State           SvcReps   SCM Size SCM Used SCM Imbalance NVME Size NVME Used NVME Imbalance Disabled UpgradeNeeded? Rebuild State
----- ----                                 -----           -------   -------- -------- ------------- --------- --------- -------------- -------- -------------- -------------
p1    cdf27ec1-ed97-4aa6-a766-39a2ed2136a1 Ready           [0-1,4-6] 105 GB   37 GB    28%           0 B       0 B       0%             0/64     None           done
p2    dcfe63e0-e10f-464d-b18c-0915e52e048c TargetsExcluded [0-2,6-7] 49 GB    4.6 GB   62%           0 B       0 B       0%             8/64     None           busy
```

**2.** Stop all pool rebuilds with one command, and confirm they have stopped.

```bash
$ dmg system rebuild stop

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

$ dmg system reintegrate --ranks=3

$ dmg system list-pools -v
Label UUID                                 State SvcReps   SCM Size SCM Used SCM Imbalance NVME Size NVME Used NVME Imbalance Disabled UpgradeNeeded? Rebuild State
----- ----                                 ----- -------   -------- -------- ------------- --------- --------- -------------- -------- -------------- -------------
p1    cdf27ec1-ed97-4aa6-a766-39a2ed2136a1 Ready [0-1,4-6] 120 GB   42 GB    28%           0 B       0 B       0%             0/64     None           busy
p2    dcfe63e0-e10f-464d-b18c-0915e52e048c Ready [0-2,6-7] 56 GB    4.9 GB   62%           0 B       0 B       0%             0/64     None           busy
```

Wait for rebuilds to complete:

```bash
$ dmg system list-pools -v
Label UUID                                 State SvcReps   SCM Size SCM Used SCM Imbalance NVME Size NVME Used NVME Imbalance Disabled UpgradeNeeded? Rebuild State
----- ----                                 ----- -------   -------- -------- ------------- --------- --------- -------------- -------- -------------- -------------
p1    cdf27ec1-ed97-4aa6-a766-39a2ed2136a1 Ready [0-1,4-6] 120 GB   42 GB    31%           0 B       0 B       0%             0/64     None           done
p2    dcfe63e0-e10f-464d-b18c-0915e52e048c Ready [0-2,6-7] 56 GB    4.9 GB   62%           0 B       0 B       0%             0/64     None           done
```


## Rebuild Stop Command Errors

The rebuild stop command may return errors when it is issued at a time that it
may not be able to handle the request. For example:

### No Rebuild Currently Running

When no rebuild is currently running, the command will report a "nonexist"
error:

```bash
# system command applying to 2 pools
$ dmg system rebuild stop
System-rebuild stop request succeeded on 0 pools
ERROR: dmg: system rebuild stop failed: pool-rebuild stop failed on pool p1: pool-rebuild stop failed: DER_NONEXIST(-1005): The specified entity does not exist, pool-rebuild stop failed on pool p2: pool-rebuild stop failed: DER_NONEXIST(-1005): The specified entity does not exist

# single pool command
$ dmg pool rebuild stop p2
ERROR: dmg: pool-rebuild stop failed: DER_NONEXIST(-1005): The specified entity does not exist
```

### Rebuild Finished, Reclaim in Progress

When the rebuild stage has successfully finished and is in its reclaim cleanup
stage, `dmg` will report a busy error. For example when pools `p1` and `p2` are
both done rebuilding and in the reclaim stage:

```bash
$ dmg system rebuild stop
System-rebuild stop request succeeded on 0 pools
ERROR: dmg: system rebuild stop failed: pool-rebuild stop failed on pool p1: pool-rebuild stop failed: DER_BUSY(-1012): Device or resource busy, pool-rebuild stop failed on pool p2: pool-rebuild stop failed: DER_BUSY(-1012): Device or resource busy
```

### Rebuild Failed, Fail\_reclaim in Progress

When the rebuild stage has finished (but failed), and is in its `Fail_reclaim`
cleanup stage, `dmg` will report a no permissions error, `-DER_NO_PERM`.

In this scenario, the admin can wait for the rebuild to be retried, and then
reissue the "rebuild stop" command:

```bash
# multiple command invocations to query pool rebuild status
$ dmg pool query <pool_label>
# Possibly observe Rebuild failed, status=<nonzero_error_code>
# Observe Rebuild busy (representing the retried rebuild)

$ dmg pool rebuild stop <pool_label>
```

Or, if the `Fail_reclaim` itself is failing and retrying, resulting in
`-DER_NO_PERM` in every attempt to stop the rebuild, then the force option can
be used to terminate the bad loop:

```bash
$ dmg pool rebuild stop --force <pool_label>
```
