# Detailed Pool Operations: Self-Healing (Rebuild) Configuration

## Introduction

This section describes some of the mechanisms using the `dmg` tool that can be
employed to manage DAOS' self-healing feature. As a reminder, the system
consists of a set of DAOS engine instances that join the system when they are
started. An engine is assigned a numeric rank upon initially joining the system.
This document may frequently interchange the term "rank" and "engine" to refer
to a DAOS engine.

By default, the system works as previous versions (per-pool `self_heal`
property) if one doesn't modify the "new" system `self_heal` property. An
administrator doesn't have to use the new system `self_heal` after upgrading to
DAOS version 2.8 and instead can choose to adopt it in the administrator's
workflow when the need arises.

DAOS behavior can be managed through "system" and "pool" properties.

### System and Per-pool Properties

System-level properties apply to system behavior and examples include system
name and pool scrub mode. System-level properties are shared and centralized in
the control-plane management service and can be read by the code in the DAOS
engines as needed. Some properties are also writable. The administrator can read
and write system properties using `dmg system get-prop` / `dmg system set-prop`.

Pool-level properties apply to pool-specific behavior and each pool has its own
set of pool-properties, which can be read and written by the system
administrator using `dmg pool get-prop` / `dmg pool set-prop`.

### Self-heal Properties

The `self_heal` property describes behavior associated with self-healing and can
be set at both the system and pool levels. For a given pool to perform the
recovery actions (map change due to excluded engine/targets, and rebuild), the policy
needs to be enabled at both the system and per-pool level.

The system level property has flags `exclude;pool_exclude;pool_rebuild` which
indicate the following:

* **`exclude`** — Whether engines get excluded from the system membership based
  on (SWIM) activity detection.
* **`pool_exclude`** — Whether when engine/target states change (e.g., as a
  result of engine exclusion from the system), all relevant affected pools' pool
  maps will be automatically updated (or not).
* **`pool_rebuild`** — Whether, following a pool map update, rebuild activities
  automatically get triggered (or not) for the affected pools.

The pool level property has flags `exclude;rebuild;delay_rebuild` which indicate
the following:

* **`pool.exclude`** — Related to the `system.pool_exclude` property and
  together they determine if a specific pool's pool map will be updated.
  Automatic pool map change happens iff `system.pool_exclude` and
  `pool.exclude` are set to true.
* **`pool.rebuild`** — Related to the `system.pool_rebuild` property and
  together they determine if rebuild activity will be automatically triggered
  upon a pool map update. Automatic rebuild on a pool will happen iff
  `system.pool_rebuild` and `pool.rebuild` are set to true.
* **`delay_rebuild`** — Mutually exclusive with `rebuild` and specifies that
  rebuild does not necessarily trigger automatically and can be delayed based on
  user requirements. Delay rebuild is mostly out of scope for this section.

On starting a DAOS system and pool creation, default `self_heal` flags will be
set as follows:

* **System-level:** `exclude;pool_exclude;pool_rebuild`
* **Pool-level:** `exclude;rebuild`

The following sections describe different administration scenarios, and how
self-heal property settings can be applied in each case. When a scenario
describes loss of an engine and waiting for a "detection delay", the delay
refers roughly to the amount of time for:

* Two SWIM protocol periods to occur,
* Plus a SWIM suspicion timeout to occur,
* Plus a `CRT_EVENT_DELAY` number of seconds to elapse, during which failure
  events can be aggregated into a logical event,
* Plus some margin of error.


## Pool Query Data Redundancy Status

**Available in:** DAOS 2.6+

The `dmg pool query` command displays the pool's data redundancy status as part
of the health information output. This field provides a clear indication of
whether the pool has sufficient target availability to maintain data redundancy.

### Output Field

The `Data redundancy` field appears in the pool health information section:

```
Pool health info:
- Rebuild idle
- Data redundancy: normal
```

or when targets are excluded and a corresponding rebuild has not yet completed:

```
Pool health info:
- Rebuild busy, 42 objs, 21 recs
- Data redundancy: degraded
```

### Field Values

| Value | Meaning |
|-------|---------|
| `normal` | No targets are DOWN; data redundancy is intact |
| `degraded` | One or more targets are DOWN; data redundancy is compromised |

### When to Check This Field

1. **After automatic or manual exclusion** — Confirm that exclusion has completed
2. **When self-heal is configured without automatic rebuild** — Verify exclusion
   occurred even when rebuild doesn't start automatically (e.g., when using
   `exclude` without `rebuild` flags)
3. **Before manually triggering rebuild** — Confirm that exclusion is complete
   and degraded state exists
4. **After rebuild completion** — Verify that redundancy has been restored to
   `normal` status
5. **During troubleshooting** — Quick health check without parsing complex output

### Example: Verifying Exclusion with Exclude-Only Policy

When the self-heal policy has `exclude` but not `rebuild` (either at system or
pool level), the `Data redundancy` field confirms that exclusion occurred:

```bash
$ dmg pool query my_pool
Pool 6f450a68-8c7d-4da9-8900-02691650f6a2, ntarget=8, disabled=1, state=TargetsExcluded
Pool health info:
- Disabled ranks: 3
- Rebuild idle
- Data redundancy: degraded  ← Confirms exclusion completed
```

The combination of:
- `Disabled ranks: 3` — Shows which rank was excluded
- `Rebuild idle` or `Rebuild done`— idle if pool has not been rebuilt since system start, done if it has been rebuild for a prior change. No new automatic rebuild triggered for this exclusion (expected with exclude-only policy)
- `Data redundancy: degraded` — Confirms data redundancy is impaired and manual
  intervention is needed

To restore redundancy for the excluded targets, manually trigger rebuild. Refer to [Detailed Pool Operations: Interactive Rebuild Controls](rebuild_controls.md) for more information about rebuild start commands.

```bash
$ dmg pool rebuild start my_pool
```

Monitor until `Data redundancy` changes from `degraded` to `normal`:

```bash
$ dmg pool query my_pool
Pool 6f450a68-8c7d-4da9-8900-02691650f6a2, ntarget=8, disabled=1, state=Ready
Pool health info:
- Disabled ranks: 3
- Rebuild done, 1042 objs, 2184 recs
- Data redundancy: normal  ← Redundancy restored
```


## System / Pool Creation and Disabling / Enabling Self-Heal

The following are example steps for managing `self_heal` policies. This
describes a very simple workflow example which disables self-healing behavior
during a typical cluster setup so that automatic exclusion and rebuild
activities are disabled until the system and pools have been brought up and are
ready for use.

**1.** Display system `self_heal` property value. The default starting value for
system self-heal is to have all flags set.

```bash
$ dmg system get-prop self_heal
Name                                        Value
----                                        -----
Self-heal policy for the system (self_heal) exclude;pool_exclude;pool_rebuild
```

**2.** Disable system self-heal features while bringing up system. This will
prevent automatic exclusions and rebuild at the system level during periods of
flux in the system at the time of start-up.

```bash
$ dmg system set-prop self_heal:'none'
system set-prop succeeded

$ dmg system get-prop self_heal
Name                                        Value
----                                        -----
Self-heal policy for the system (self_heal) none
```

**3.** Add ranks to system by starting storage server DAOS services and
formatting so that they join.

Use `daos_server start` and `dmg storage format` commands.

**4.** Run system query which will indicate disabled system `self_heal` flags.

```bash
$ dmg system query
Rank  State
----  -----
[0-3] Joined
System property self_heal flags disabled: exclude, pool_exclude, pool_rebuild
```

**5.** Set system `self_heal.exclude` flag so inactive ranks get excluded from
the system membership. This can be enabled once all ranks have been
successfully joined and stable to avoid flux during system join phase. System
`self_heal.pool_exclude` and `self_heal.pool_rebuild` flags are disabled and
can be re-enabled after pools have been created.

```bash
$ dmg system set-prop self_heal:'exclude'
system set-prop succeeded

$ dmg system query
Rank  State
----  -----
[0-3] Joined
System property self_heal flags disabled: pool_exclude, pool_rebuild
```

**6.** Create two pools using half of the available space across all joined
engines in the system.

```bash
$ dmg pool create first_pool -z 50%
Creating DAOS pool with 50% of all storage
Pool created with 4.44%,95.56% storage tier ratio
-------------------------------------------------
  UUID                 : 1c8f6a4e-a4eb-418d-bedc-0c7751e41af1
  Service Leader       : 3
  Service Ranks        : [0-3]
  Storage Ranks        : [0-3]
  Total Size           : 34 TB
  Storage tier 0 (SCM) : 1.5 TB (372 GB / rank)
  Storage tier 1 (NVMe): 32 TB (8.0 TB / rank)

$ dmg pool create second_pool -z 100%
Creating DAOS pool with 100% of all storage
Pool created with 4.43%,95.57% storage tier ratio
-------------------------------------------------
  UUID                 : ce3a6a74-8a19-4c6e-a3ef-1ee85d8e06f1
  Service Leader       : 2
  Service Ranks        : [0-3]
  Storage Ranks        : [0-3]
  Total Size           : 34 TB
  Storage tier 0 (SCM) : 1.5 TB (371 GB / rank)
  Storage tier 1 (NVMe): 32 TB (8.0 TB / rank)
```

**7.** Once pools have been successfully created, `dmg pool query` output
indicates the disabled system-property flags that will prevent pool map
exclusion and pool rebuild. Then, enable all system self-heal features and show
`dmg system query` displays no self-heal disabled flags in its output. And,
`dmg pool query` output following the system property change will no longer
report disabled exclude and rebuild.

```bash
$ dmg pool query first_pool
Pool ce3a6a74-8a19-4c6e-a3ef-1ee85d8e06f1, ntarget=16, disabled=0, leader=1, version=1, state=Ready
Pool health info:
- Rebuild idle, 0 objs, 0 recs
Pool space info:
- Target count:16
- Storage tier 0 (SCM):
  Total size: 1.5 TB
  Free: 1.4 TB, min:88 GB, max:88 GB, mean:88 GB
- Storage tier 1 (NVME):
  Total size: 32 TB
  Free: 32 TB, min:2.0 TB, max:2.0 TB, mean:2.0 TB
exclude disabled on pool due to [system] policy
rebuild disabled on pool due to [system] policy

$ dmg system set-prop self_heal:'exclude;pool_exclude;pool_rebuild'
system set-prop succeeded

$ dmg system query
Rank  State
----  -----
[0-3] Joined

$ dmg pool query first_pool
Pool 1c8f6a4e-a4eb-418d-bedc-0c7751e41af1, ntarget=16, disabled=0, leader=0, version=1, state=Ready
Pool health info:
- Rebuild idle, 0 objs, 0 recs
- Data redundancy: normal
Pool space info:
- Target count:16
- Storage tier 0 (SCM):
  Total size: 1.5 TB
  Free: 1.4 TB, min:88 GB, max:88 GB, mean:88 GB
- Storage tier 1 (NVME):
  Total size: 32 TB
  Free: 32 TB, min:2.0 TB, max:2.0 TB, mean:2.0 TB
```

**8.** Then disable pool `self_heal.exclude` and `self_heal.rebuild` flags on
`first_pool` to stop rank inactivity from triggering pool exclusion and/or
rebuild during maintenance of `first_pool`.

```bash
$ dmg pool get-prop first_pool self_heal
Pool first_pool properties:
Name                            Value
----                            -----
Self-healing policy (self_heal) exclude;rebuild

$ dmg pool set-prop first_pool self_heal:'none'
pool set-prop succeeded

$ dmg pool get-prop first_pool self_heal
Pool first_pool properties:
Name                            Value
----                            -----
Self-healing policy (self_heal) none
```

**9.** Compare `dmg pool query` output for both pools and notice that self-heal
policy disable messages are output only for `first_pool`, due to the difference
in pool property values for each.

```bash
$ dmg pool query first_pool
Pool 1c8f6a4e-a4eb-418d-bedc-0c7751e41af1, ntarget=16, disabled=0, leader=0, version=1, state=Ready
Pool health info:
- Rebuild idle, 0 objs, 0 recs
- Data redundancy: normal
Pool space info:
- Target count:16
- Storage tier 0 (SCM):
  Total size: 1.5 TB
  Free: 1.4 TB, min:88 GB, max:88 GB, mean:88 GB
- Storage tier 1 (NVME):
  Total size: 32 TB
  Free: 32 TB, min:2.0 TB, max:2.0 TB, mean:2.0 TB
exclude disabled on pool due to [pool] policy
rebuild disabled on pool due to [pool] policy

$ dmg pool query second_pool
Pool ce3a6a74-8a19-4c6e-a3ef-1ee85d8e06f1, ntarget=16, disabled=0, leader=1, version=1, state=Ready
Pool health info:
- Rebuild idle, 0 objs, 0 recs
- Data redundancy: normal
Pool space info:
- Target count:16
- Storage tier 0 (SCM):
  Total size: 1.5 TB
  Free: 1.4 TB, min:88 GB, max:88 GB, mean:88 GB
- Storage tier 1 (NVME):
  Total size: 32 TB
  Free: 32 TB, min:2.0 TB, max:2.0 TB, mean:2.0 TB
```

**10.** Enable pool policy flags and show `dmg pool query` command no longer
displays any self-heal "disabled policy" messages.

```bash
$ dmg pool set-prop first_pool self_heal:'exclude;rebuild'
pool set-prop succeeded

$ dmg pool query first_pool
Pool 1c8f6a4e-a4eb-418d-bedc-0c7751e41af1, ntarget=16, disabled=0, leader=0, version=1, state=Ready
Pool health info:
- Rebuild idle, 0 objs, 0 recs
- Data redundancy: normal
Pool space info:
- Target count:16
- Storage tier 0 (SCM):
  Total size: 1.5 TB
  Free: 1.4 TB, min:88 GB, max:88 GB, mean:88 GB
- Storage tier 1 (NVME):
  Total size: 32 TB
  Free: 32 TB, min:2.0 TB, max:2.0 TB, mean:2.0 TB
```


## Planned, Online, Small-Scale System Maintenance

### Scenario

The following are example steps for managing a short maintenance task (e.g.,
rebooting a single server, or stopping/restarting a small number of engines)
while:

* Keeping the DAOS system online and available for ongoing application I/O,
  even in "degraded mode" (when pools do not have their original degree of data
  redundancy).
* Not triggering a rebuild on all pools as a result of the brief maintenance.

The maintenance must not exceed pool redundancy factors (or the system
`DAOS_POOL_RF`) for applications to continue running in degraded mode. At the
conclusion of maintenance, engines are brought back into the system and
reintegrated into all affected pools (exiting degraded mode).

### Available Actions

* At the system level, disable `system self_heal.pool_rebuild`.
* Upon restart of the affected resource, reintegrate its engine(s) back into
  the affected pools.
* Re-enable `system self_heal.pool_rebuild` and trigger a re-evaluation of the
  property.

### Steps

Begin the maintenance by disabling `system.self_heal.pool_rebuild`:

```bash
$ dmg system set-prop self_heal:'exclude;pool_exclude'
```

System and per-pool exclude remain enabled, important for allowing ongoing
application I/O to proceed (without timeouts) while pools are in degraded mode.

Stop the rank X engine and wait for the detection delay:

```bash
$ dmg system stop --ranks=X
```

* Rank X excluded from the system and from all pools using rank X targets for
  storage. Pools operating in degraded mode.
* No rebuild occurs.

Start the rank X engine and wait for it to join the system. In this example,
assume `pool_A` uses engine rank X targets for storage.

```bash
$ dmg system start --ranks=X
$ dmg system query
```

* Rank X no longer excluded from the system.

```bash
$ dmg pool query-targets pool_A --rank=X
```

* Rank X targets in the relevant pools' maps are still in a DOWN state (and
  require manual reintegration).

Reintegrate the rank X engine into all affected pools:

```bash
$ dmg system reintegrate --ranks=X
```

* Rank X reintegrated in all relevant pools.
* Rank X rebuilding in all relevant pools.

End the maintenance by enabling all system `self_heal` flags and re-evaluating
the assignment:

```bash
$ dmg system set-prop self_heal:'exclude,pool_exclude,pool_rebuild'
$ dmg system self-heal eval
```

* No rebuild.
* Any future (unplanned) faults will result in system exclusion, pool map
  exclusion, and rebuild launched on the affected pools.


## Planned, Offline, Large-Scale System Maintenance and Subsequent Restart

### Scenario

A large number of resources (`daos_server` or engine instances) will be stopped
for some time to perform maintenance. Or, perhaps a large planned
fabric/network disruption is scheduled that will disrupt connectivity to a large
number of engines. The number of faults this would normally generate will exceed
the system-wide setting for the number of tolerable failures (see engine
environment variable `DAOS_POOL_RF`). As a result, the storage system will
effectively be offline. System exclusions, pool map updates and rebuilding is
not wanted for reasons such as:

* During the stoppage, if the DAOS mechanism to aggregate multiple engine
  failures into a single logical event does not get processed as a single
  event, an initial small number of failures (< `DAOS_POOL_RF`) can trigger
  pool map updates and rebuilding activity.
* During restart, should a single or small number of engines
  (< `DAOS_POOL_RF`) encounter an unexpected failure, system/pool map and
  rebuilding activity would be triggered, though counterproductive — especially
  if the faults can be quickly remedied and affected engines restarted.

### Available Actions

The admin can disable system `self_heal` (disable all flags). And can re-enable
all flags after confirming all affected resources are running properly following
the maintenance. After re-enabling system `self_heal`, the admin must trigger a
re-evaluation of the modified property.

### Steps

**1.** Begin the maintenance by disabling self-heal:

```bash
$ dmg system set-prop self_heal:'none'
```

**2.** Perform planned maintenance. Shut down selected engines and wait for the
detection delay.

```bash
$ dmg system stop --ranks=<more_than_RF_ranks>
```

**3.** Restart the engines that were stopped, then wait for all ranks to be
joined to the system.

```bash
$ dmg system start --ranks=<above>
$ dmg system query
```

**4.** End the maintenance by re-enabling self-heal:

```bash
$ dmg system set-prop self_heal:'exclude;pool_exclude;pool_rebuild'
$ dmg system self-heal eval
```


## Planned, Normal System Restart

### Scenario

This is similar to the previous scenario, but involving a whole DAOS system
restart. Again on restart, we do not want any unexpected single or small number
of failures (< `DAOS_POOL_RF`) to cause system / pool exclusions (and
rebuilding) activity.

### Available Actions

Same as the previous scenario.

### Steps

**1.** Begin the maintenance by disabling self-heal:

```bash
$ dmg system set-prop self_heal:'none'
```

**2.** Perform planned maintenance. Shut down all engines in the system.

```bash
$ dmg system stop
```

**3.** Restart the engines, then wait for all ranks to be joined to the system.

```bash
$ dmg system start
$ dmg system query
```

**4.** End the maintenance by re-enabling self-heal:

```bash
$ dmg system set-prop self_heal:'exclude;pool_exclude;pool_rebuild'
$ dmg system self-heal eval
```


## Problematic Pool

### Scenario

Consider a system with pools P and Q. For some reason, pool P will not be able
to rebuild correctly (e.g., rebuild runs out of space due to insufficient `space_rb`
property setting during pool create). In this case, a mechanism to disable rebuild for
pool P is needed.

### Available Actions

Pool P `self_heal` property can be set such that rebuild is disabled.

### Steps

**1.** Disable rebuild on pool P:

```bash
$ dmg pool set-prop self_heal:'exclude' P
```

**2.** Simulate a fault by stopping the rank X engine and wait for the
detection delay.

```bash
$ dmg system stop --ranks=X
```

* Rank X is excluded from the system and from pools P and Q.
* Rank X is rebuilding in pool Q only. Pool P will remain in degraded mode.

**3.** Verify exclusion status using `dmg pool query`:

For pool P (rebuild disabled):
```bash
$ dmg pool query P
Pool health info:
- Disabled ranks: X
- Rebuild idle
- Data redundancy: degraded  ← Pool remains degraded (rebuild disabled)
```

For pool Q (rebuild enabled):
```bash
$ dmg pool query Q
Pool health info:
- Disabled ranks: X
- Rebuild busy, 42 objs, 21 recs
- Data redundancy: degraded  ← Rebuilding in progress
```

After pool Q rebuild completes:
```bash
$ dmg pool query Q
Pool health info:
- Disabled ranks: X
- Rebuild done, 1042 objs, 2184 recs
- Data redundancy: normal  ← Redundancy restored
```

**4.** To restore pool P's redundancy later, manually trigger rebuild:

```bash
$ dmg pool rebuild start P
```

Monitor until `Data redundancy` changes to `normal`:
```bash
$ dmg pool query P
Pool health info:
- Disabled ranks: X
- Rebuild done, 1042 objs, 2184 recs
- Data redundancy: normal  ← Now restored
```
