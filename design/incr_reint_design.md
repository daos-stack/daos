# Incremental Reintegration (Tech Preview) Design

## Summary

- **Problem**:
  When a storage target suffers a transient, recoverable failure, the usual response is to
  exclude it temporarily and reintegrate it shortly after it has been repaired. The current
  full-reintegration mode discards all local data and rebuilds the target from scratch, which
  does not meet the performance and usability expectations for this scenario. This design
  extends the reintegration protocol with an incremental mode that retains the majority of the
  existing data and migrates only the data generated after the exclusion, while guaranteeing
  data integrity.

- **Proposal**:
  Maintain a system-wide *global stable epoch* per container, below which all data is
  consistently committed or aborted on every healthy shard. Each target stores the epoch
  persistently while it is healthy, and on reintegration only data above the stored epoch is
  migrated.

  Containers may be destroyed or created while a target is excluded. A container recovery
  process, executed as the first step of reintegration, reconciles the target's container shards
  with the authoritative container list.

  Objects, dkeys, akeys and records may be punched while a target is excluded, and the evidence
  of a punch may be removed by normal VOS aggregation. The rebuild path is extended to discover
  and replay such punches, and aggregation is extended to retain the evidence they need for as
  long as an excluded target may need it, within a bound set by space pressure.

  For the common case in which no update has occurred since the exclusion, a fast mode
  recognizes the situation from one persistent value per container shard and lets the healthy
  targets skip the scan phase, so that the target is reintegrated almost immediately.

- **Impact**:
  Reintegration becomes substantially faster in most cases, in particular when little or no data
  has been updated after the exclusion, making recovery from typical transient failures quicker
  and simpler to operate.

## Background

In the current version of DAOS, an engine that is excluded and then reintegrated discards all of
its existing data and reconstructs it from scratch. This mechanism is referred to as the
full-reintegration mode. Its duration is proportional to the amount of data held by the target,
roughly the target capacity divided by the available rebuild bandwidth, and can be very long when
the target is nearly full. When the failure is a temporary outage, such as a power loss or a
daemon crash, most of that data movement is unnecessary.

Container existence is likewise handled implicitly: all local containers are destroyed, and VOS
containers are re-created during reintegration only as data is migrated into them. A container
that has no data to migrate to the reintegrating engine is therefore never re-created there, and
subsequent I/O to it fails.

*Incremental reintegration* addresses these limitations: committed data is retained in place,
only the data updated after the exclusion is reconstructed, and the existence of containers,
objects and keys is kept consistent with the rest of the system.

## Goals and Non-Goals

### Goals

1. The majority of the data is retained across reintegration. To keep the protocol simple, it is
   acceptable to re-migrate a small portion of recently written data that would not strictly need
   to be moved.
2. Container existence is consistent between the reintegrated engines and the rest of the system,
   given that containers may be destroyed and created while an engine is excluded.
3. Object, dkey and akey existence is consistent, given that existing ones may be punched and new
   ones created while an engine is excluded.
4. Data correctness after reintegration is guaranteed, including the cases in which records are
   punched, written or overwritten while an engine is excluded.
5. Reintegration time is reduced compared to the full-reintegration mode, in particular when
   little or no data has changed after the exclusion.

### Non-Goals

- **Replacing full reintegration.** The full-reintegration mode remains available and remains
  the fallback whenever incremental reintegration cannot be applied: pools created before the
  required durable-format version, targets whose local data is missing or unusable, objects
  whose punch evidence has been discarded by aggregation, and targets whose stored stable epoch
  lies below a container-level punch watermark. Incremental reintegration is an optimization for
  the common transient-failure case, not a guarantee for every object.
- **Zero data movement.** Every update above the global stable epoch is migrated, and the data
  between a target's own local stable epoch and the global stable epoch is treated as
  potentially divergent and pulled again. Minimizing that window is not an objective of this
  design.
- **Preserving uncommitted data on the excluded target.** Data above the stored global stable
  epoch on a reintegrating target is not trusted; it is overwritten or removed as the migrated
  range is applied. No attempt is made to reconcile it with the surviving shards.
- **Changing the exclusion and rebuild-on-failure path.** The rebuild that runs when a target is
  excluded is unchanged. Only the reintegration direction, from surviving shards back to the
  returning target, is modified.
- **Changing snapshot, aggregation or I/O path semantics** beyond what is strictly required to
  retain and report punches. Space reclamation remains a first-class requirement; punch evidence
  is retained only while affordable, as defined by the tiered retention policy.
- **New client-visible APIs.** The changes are confined to the control plane, the rebuild
  protocol and VOS metadata. Applications observe only a faster reintegration.
- **Other pool map operations.** Drain, extend and target replacement with fresh storage are not
  affected; they continue to use the existing full migration paths.

## Design

The design consists of four components, each described in its own section:

1. **Global stable epoch.** A per-container epoch, agreed system-wide and stored on every
   target, below which no data needs to be migrated on reintegration.
2. **Container recovery.** Reconciliation of a reintegrating target's container shards with the
   authoritative container list before any data migration.
3. **Punch handling.** Discovery, replay and retention of object, dkey, akey and record punches
   that occurred while the target was excluded.
4. **Fast reintegration mode.** A shortcut for the case in which nothing was updated during the
   exclusion.

Together they change the reintegration of a target into the following sequence. While the target
is healthy, the global stable epoch of every container is agreed and stored on it. When the
target is reintegrated, container recovery first reconciles its container shards with the pool
service and collects its stored stable epochs; the pool map is then updated to bring the target
back into the layout; the healthy targets scan their objects for shards that belong to the
returning target, unless fast mode allows them to skip the scan; the returning target pulls,
for each such object, only the range above its stored stable epoch, including the punches that
occurred in that range; and a final reclaim removes the copies that were rebuilt elsewhere while
the target was excluded.

Throughout the document, `S` denotes the global stable epoch stored on the reintegrating target
for the container under discussion. It is the lower bound of every migrated range.

### Global Stable Epoch and Incremental Migration

The foundation of incremental reintegration is a new per-container, system-wide *global stable
epoch*: an epoch below which every modification is guaranteed to be persistently and consistently
applied on all healthy shards of the container. Data below that epoch does not need to be
migrated back when a target rejoins the system; only the updates above it have to be
reconstructed.

The epoch is computed bottom-up in three steps, per VOS container shard, per engine, and then on
the pool service (PS) leader, and is then pushed back down and stored persistently on each target.
Wherever possible the existing EC aggregation epoch reporting infrastructure is extended rather
than introducing new RPCs or ULTs.

#### Local Stable Epoch (Per VOS Container Shard)

A new VOS API, `vos_cont_get_local_stable_epoch()`, derives a *local stable epoch* for a
container shard from its DTX state. It returns a conservative estimate of the highest epoch below
which all DTX entries are already committed or aborted, so that no in-flight transaction can
still change the data below it. The estimate is bounded by the oldest entry of the sorted,
unsorted and re-indexed DTX lists, and by the aggregation gap `vos_agg_gap`.

Querying the local stable epoch cannot be a pure read: to make the returned epoch genuinely
*stable*, the call must also raise the container's acceptable modification boundary. Once the
boundary is raised, the object I/O layer rejects any further modification with an older
epoch and restarts the corresponding DTX with a newer one. This is the property the whole design
relies on: once an epoch has been reported, the data below it can no longer change.

To avoid an additional periodic ULT, the query is driven by the existing per-container VOS
aggregation ULT, which caches the result so that the reporting path can consume it without
issuing further queries.

#### Engine-Wide Reporting to the PS Leader

The existing per-pool epoch reporting ULT, which today reports the EC aggregation boundary every
few seconds, is extended to also report the stable epoch. For every container the engine
hosts, it scans the local container shards, skips the failed ones (targets in `DOWN` or
`DOWNOUT` state) and takes the minimum local stable epoch among the remaining healthy shards.

That engine-wide minimum is sent to the PS leader through a new IV class,
`IV_CONT_TRACK_EPOCH_REPORT`, carried in the same message as the EC aggregation boundary epoch.
Reusing the existing reporting path keeps the additional network and CPU cost of the design
close to zero.

#### Global Stable Epoch Calculation on the PS Leader

On the PS leader engine, the per-container-service epoch tracking ULT is extended to
maintain the stable epoch alongside the EC aggregation boundary. On each round it walks the
epochs reported by all engines, ignores the ranks known to have failed, and picks the
*system-wide minimum* local stable epoch as the new global stable epoch of the container.

Because the value is a minimum over all healthy shards, it is by construction an epoch below
which every shard has already stabilized its data. The leader then broadcasts it to all
engines in the system through a new IV class, `IV_CONT_TRACK_EPOCH`. The same broadcast carries
the punch evidence release bit specified in the punch section, which the leader sets only when
every target of the pool is `UPIN` and no report was missing from the round.

#### Persisting the Global Stable Epoch on Each Target

On receiving the broadcast, each engine dispatches the new epoch to all of its local targets,
which store it persistently, so that it survives the crash or power loss that caused the exclusion
in the first place. A new field, `ced_global_stable_epoch`, is added to the VOS container
durable-format extension for this purpose, together with the accessors
`vos_cont_set_global_stable_epoch()` and `vos_cont_get_global_stable_epoch()`. The value is
also cached in memory to keep the reintegration path cheap.

Two rules must guard the update:

- **Only `UPIN` targets may be updated.** A target that is not in `UPIN` state did not
  participate in the calculation of the new global stable epoch, so applying it there would be
  unsafe.
- **The epoch must never roll back and must never exceed the local stable epoch.** The setter
  rejects an epoch that is older than the currently stored one or newer than the shard's own
  local stable epoch with `-DER_NO_PERM`.

Because the durable format changes, backward compatibility has to be handled explicitly:
containers created before the new pool durable-format version have no extension to store the
epoch in. For those, the getter returns 0, which naturally degrades to the existing full
reintegration behavior and lets an upgraded system keep working with old pools.

#### Using the Global Stable Epoch During Reintegration

Once an engine or a target is excluded, it stops being `UPIN` and therefore stops receiving epoch
updates, so its stored `ced_global_stable_epoch` is frozen at the last value agreed upon
while it was still healthy. That frozen value is precisely the point from which the target's data
may have diverged from the rest of the system.

The object migration path is therefore changed to query the stored global stable epoch and
use it as the lower bound of the migration epoch range, instead of always starting from epoch 0:

- If the object already exists on the local target, only the range above the global stable epoch
  is migrated. Snapshots older than the stable epoch are skipped, and a recorded object
  punch is replayed only if it happened above the stable epoch.
- If the object does not exist locally at all (it was created after the stable epoch, or it
  landed on this target because of co-location), the lower bound falls back to 0 and the whole
  object is migrated.

With this approach the majority of the data, which is already committed below the global stable
epoch, is retained in place, and only the incrementally updated data above it has to be
moved back. This is what makes reintegration incremental.

### Container Recovery Process

Because incremental reintegration no longer wipes out the local data, the VOS container
shards that the excluded target kept on disk survive the outage. In the meantime, the rest of the
system keeps serving container create and destroy requests, so when the target comes back its set
of local container shards may diverge from the authoritative container list held by the pool
service (PS):

- **Stale shards.** A container that was destroyed while the target was down would still have a
  local shard. Without cleanup it would be resurrected as an orphan that consumes space and can
  still be reached by subsequent I/O.
- **Missing shards.** A container that was created while the target was down would have no local
  shard. In the full-reintegration mode the shard is created implicitly when data is migrated
  into it, but a freshly created container may have no data to migrate at all, in which case the
  shard would never be created and subsequent I/O to that target would fail.

To close this gap, a new container recovery step is introduced and executed as the first step
of reintegration, before the pool map is updated and before any data migration starts.

#### Placement in the Reintegration Flow

Container recovery is hooked into the common preparation path shared by the reintegration
and extension operations. The ordering is a hard requirement of the design:

1. The requested target list is validated against the authoritative pool map, producing the list
   of targets that really need to join.
2. Container recovery reconciles the container shards on those targets.
3. Leftover data is discarded, which is still required for extension and for reintegration in
   `DAOS_REINT_MODE_DATA_SYNC` mode.
4. Only then is the pool map updated to mark the targets `UP`, which makes them visible to normal
   I/O and triggers the migration.

Performing the recovery before the pool map update guarantees that no client I/O can ever reach a
target whose container shards are still inconsistent.

#### Distributing the Authoritative Container List

On the PS leader, the recovery step builds the reference view of the pool:

- The target address list is de-duplicated into a rank list, because reintegrating several
  targets of the same rank requires only one RPC per engine.
- The container list is enumerated excluding the containers whose destroy is already in progress,
  so that such containers are treated as non-existent and their shards are removed rather
  than recreated.
- The resulting container list is exported through a read-only bulk handle and sent with a new
  `POOL_RECOV_CONT` collective RPC to all the joining ranks. Bulk transfer is chosen over inline
  data so that the RPC stays small even for pools with a large number of containers.

#### Reconciliation on the Reintegrating Target

The target-side handler accesses the bulk buffer and runs the reconciliation on every local
target in parallel. Each target performs a two-way reconciliation:

1. **Filter.** The target first checks that it is actually a member of the recovery list; healthy
   targets of the same engine skip the work.
2. **Create the missing shards.** For every container in the list received from the PS leader,
   the local VOS container shard is created if it does not exist yet.
3. **Destroy the orphan shards.** The list is loaded into a temporary in-memory tree keyed by
   container UUID, and the local containers are enumerated. Any shard that is not found in the
   tree is an orphan and is destroyed, and the enumeration is told to drop the corresponding
   entry.
4. **Report the stored stable epochs.** While enumerating, the target takes the minimum of the
   global stable epochs stored in the shards it already held, for use by fast reintegration mode.
   Shards created in step 2 are marked as such and excluded from the minimum.

The engine returns the minimum over its reintegrating targets in its reply. Once all targets are
reconciled, the engine also cleans up its IV cache, so that no stale container metadata cached
before the exclusion can be reused after the target rejoins.

A request flag selects a *query-only* variant of the RPC that performs step 4 alone, without
creating or destroying any shard. The fast reintegration mode section describes when the leader
uses it.

#### Races Against Concurrent Container Create and Destroy

Container recovery cannot be made atomic with respect to the rest of the system: a container may
be created or destroyed while the `POOL_RECOV_CONT` RPC is in flight, which would silently make
the distributed list obsolete. Two mechanisms are proposed to protect against this.

**A `recov_cont` pool property.** Before sending the RPC, the PS leader sets a new
`recov_cont` RDB property. The container create and destroy paths reset it within their own
RDB transaction, so that any such operation leaves a durable trace. When the reintegration later
updates the pool map, it re-checks the property: if it has been reset (or removed) in the
meantime, a container create or destroy raced with the recovery and the whole operation fails
with `-DER_AGAIN`. The control plane maps this to a retryable status, and the reintegration
is restarted from the beginning with a fresh container list.

**A per-pool recovery lock.** The PS leader role may switch while the leader is waiting for
container recovery to complete. The new leader may mark the target `UP` earlier than the old one,
after which container target create/destroy RPCs start arriving at a target that is still
executing a now-stale recovery. To serialize these, a per-pool read-write lock is taken in
write mode around the recovery collective, and in read mode by the discard path, so that
recovery, discard and normal container operations cannot interleave on the same engine.

With this process, the container namespace of a reintegrated target is guaranteed to match the
authoritative view of the pool service before any data is migrated, which satisfies the container
existence consistency goal.

### Object, Key and Record Punch Process

Retaining the existing data on a reintegrating target, instead of wiping it, introduces a
consistency problem below the container level. While a target is excluded, objects, dkeys, akeys
and records on the surviving shards may be punched. A punch is a deletion: it leaves nothing
behind that a migration could copy. If the reintegrating target is not told about the punches
that occurred during its absence, the punched entities silently reappear when it rejoins, because
its own copies were kept in place.

Punches must therefore be discovered and replayed on the reintegrating target in the same way as
updates. This is possible only if two conditions hold at the time reintegration runs:

1. **Retention.** Evidence of the punch still exists on the surviving shards.
2. **Discovery.** The rebuild machinery reports that evidence to the reintegrating target.

This section examines both conditions for each kind of punch against the current implementation,
states the resulting problem, and then specifies the changes required to close the gaps.

#### Current Behavior

##### Punch Representation

DAOS uses two fundamentally different representations for punches.

**Object, dkey and akey punches are entries in the incarnation log (ilog).** A single VOS punch
entry point handles all three levels, selecting the level from the arguments supplied:

| Request | Level punched |
| --- | --- |
| No dkey supplied | The object's ilog |
| dkey supplied, no akeys | The dkey's ilog |
| dkey and akeys supplied | The ilog of each named akey |

At every level the punch is recorded as an explicit ilog entry carrying the punch epoch; nothing
is deleted from the trees. A punch entry is distinguished from an update entry by carrying a
punch minor epoch instead of an update minor epoch. A punch at an upper level shadows everything
below it: an object punch hides all of the object's dkeys, akeys and records at the covered
epochs, although those subtrees remain physically present.

**Record punches are values, not ilog entries.** A record punch is issued as an update carrying a
zero-length value and is stored as an ordinary versioned record inside the akey's value tree:

- A **single-value punch** is a single-value tree record of size zero, keyed by the epoch and
  minor epoch of the operation like any other single value. Its data address carries the same
  hole flag as an array hole, so readers and the enumeration recognize both kinds of record
  punch uniformly.
- An **array extent punch** is an extent tree *hole* over the punched range, identified as a
  hole by its address.

The level at which a punch is recorded determines its scope:

| Punch | Where it is recorded | What it removes |
| --- | --- | --- |
| Object punch | Punch entry in the object's ilog | The object and everything beneath it |
| dkey punch | Punch entry in the dkey's ilog | The dkey and every akey and value beneath it |
| akey punch | Punch entry in the akey's ilog | The akey and every value beneath it |
| Single-value punch | Zero-sized record in the akey's SV tree | That value, at that epoch |
| Array extent punch | Hole extent in the akey's extent tree | That byte range, at that epoch |

An ilog punch invalidates the entity and its entire subtree; a record punch writes a tombstone
*value* under an akey that remains alive. The two are found by different means: an ilog punch by
reading the entity's ilog, a record punch by enumerating the akey's value tree.

##### Aggregation of Punches

VOS aggregation treats the two representations differently.

**Ilog punches are removed by aggregation.** Each aggregation pass covers one epoch window,
starting just above the most recent snapshot (or at epoch 0 if the container has none) and ending
at the aggregation boundary. No snapshot lies inside a window, so no reader can observe an
intermediate state within it; only the state at the end of the window is observable. Aggregation
uses this to simplify ilogs: when a key was created and then punched, and both entries fall in
the same window, the key did not exist at the end of the window, and both entries are removed. If
a snapshot separates the creation from the punch, the two fall in different windows, the snapshot
still requires the key, and the punch is kept.

When the simplification leaves an ilog empty, the corresponding object, dkey or akey entry is
deleted from its parent tree together with the whole subtree beneath it. This is the intended
space reclamation behavior: a punched key that nothing references should not consume space
indefinitely.

For reintegration the effect is that, once aggregation has processed a punched key, the punch
is gone and the key is simply absent from the tree. Absence cannot be distinguished from "this key
never existed on this shard", so an enumeration of the surviving shard cannot report the punch,
the migration has nothing to replay, and the stale copy on the reintegrating target is
resurrected.

**Record punches survive aggregation.** Because a record punch is a value, aggregation treats it
like any other value and keeps the winning one. Single-value aggregation keeps the record with
the highest committed epoch without regard to its size, so a zero-sized record that is the latest
version is retained. Extent aggregation treats a hole as a normal extent: entries covered by it
may be discarded and adjacent holes coalesced, but the effective latest hole is retained, since it
carries the deletion semantics for subsequent reads.

Record punches therefore remain enumerable after aggregation.

##### Punch Handling in Rebuild

The current rebuild path was designed for a target that starts empty. For such a target, a key
that was punched before the rebuild need not be mentioned: there is nothing on the target to
remove, and omitting the key yields the correct result. The scan and the migration enumeration
are built on this reasoning and skip punched entities instead of reporting them. A reintegrating
target that keeps its local data invalidates the reasoning: it still holds the key, and omitting
the punch leaves the key in place.

**The scan reports punched objects only when snapshots exist.** The rebuild scan walks the
object index of every container over the full epoch range and, by default, requests visible
objects only, so an object whose latest ilog entry is a punch is skipped. Punched objects are
requested only when the container has snapshots. Even then the reported entry does not carry the
punch epoch; it carries the upper bound of the scanned range, and the puller punches the object
at that epoch. This is adequate when the object is materialized only at its snapshot epochs, but
it is not a faithful replay of the punch.

**The enumeration reports only entities visible at the upper bound of the range.** The
migration enumeration lists an object's dkeys and akeys under the same visible-only rule: a key
that is punched at the upper bound of the requested range, and not re-created, is skipped. The
only punch epochs the enumeration reports are those of punches *later than the upper bound*: for
every key visible at the upper bound, the epoch of the first later punch, at object, dkey or akey
level, is attached as a separate descriptor. This is how rebuild preserves snapshots: the puller
enumerates one range per snapshot, and a key visible at a snapshot but punched afterwards is
reproduced at the snapshot and then punched at the reported epoch.

**Record punches are already discovered.** The enumeration includes visible hole extents and
zero-sized single values, reports them with a zero record size, and the puller replays them as
zero-sized updates at their original epoch.

**The puller can already replay every kind of punch.** Given an epoch and a key, the puller has
code paths to replay an object, dkey, akey or record punch. It lacks the information, not the
mechanism.

#### Problem Statement

The final range pulled by a reintegrating target starts at the global stable epoch `S` and ends
at the present. Consider a dkey or akey that existed before `S`, and was therefore kept on the
reintegrating target, and that was punched at some epoch inside that range. It is invisible at
the upper bound and has no later punch, so the enumeration does not mention it. The same holds
for a punched object, which the scan does not report unless the container has snapshots. The
stale key or object is resurrected even when aggregation has never touched its ilog. If
aggregation has run, the evidence may in addition be gone entirely.

The situation for each kind of punch is summarized below:

| Punch | Survives aggregation | Reported inside the migrated range | Action required |
| --- | --- | --- | --- |
| Record punch | Yes | Yes | None |
| Object, dkey, akey punch | No | No | Close both gaps |

Record punches require no special handling: the migration enumerates the zero-sized values and
holes above `S` in the same pass as ordinary records, and applying them on the reintegrating
target reproduces the deletion. Object, dkey and akey punches have two independent gaps: a
*discovery* gap in the scan and the enumeration, and a *retention* gap in aggregation. The
remainder of this section addresses these two gaps.

#### Design

The changes proposed for object, dkey and akey punches are summarized below and specified in the
subsections that follow.

| Area | Change |
| --- | --- |
| Rebuild scan | Report punched objects for every incremental pull, with the real punch epoch |
| Migration enumeration | Report keys punched inside the range, each with a covering punch epoch |
| Rebuild puller | Replay reported punches at their real epochs before applying records |
| VOS aggregation, tier 1 | Never remove the most recent committed punch entry of an ilog |
| VOS aggregation, tier 2 | Under space pressure, discard punches but record per-object watermarks |
| VOS aggregation, tier 3 | Under severe pressure, record a single container-level watermark |
| Rebuild fallback | Migrate in full any object whose watermark exceeds the target's `S`; re-schedule the reintegration in full mode when a container-level watermark does |
| Policy | Tier transition driven by the existing pool space pressure levels, with hysteresis |
| Release | Leader-decided release epoch, carried with the stable epoch broadcast, after which evidence below it is discarded |

##### Discovering Punches Inside the Migrated Range

The discovery gap is closed by having the existing walks report what they currently skip, rather
than by adding a walk. Three changes are required.

- **The scan reports punched objects for every incremental pull**, regardless of whether the
  container has snapshots, and reports the real epoch of the punch instead of the upper bound of
  the scanned range. The snapshot condition exists only because a target rebuilt from nothing has
  nothing to punch, which no longer holds when local data is retained.
- **The migration enumeration includes punched keys.** For an incremental pull the enumeration
  requests punched entities, so that a dkey or akey punched inside the migrated range is reported
  rather than skipped. Every reported key, whether still punched or re-created since, carries a
  **covering punch epoch**: the epoch of the key's most recent punch at or below the upper bound
  of the range, when that punch lies inside the range. The epoch is reported as a descriptor of
  its own, distinct from the later-punch descriptors used for snapshots, because the two have
  different meanings and are replayed at different points. Nothing beneath a still-punched key is
  transferred, since the punch removes it.
- **The puller replays each reported punch at its real epoch**, using the existing object, dkey
  and akey punch replay paths, before applying the records of the range.

The covering punch epoch is inexpensive to provide. The iterator already has a mode that reports
punched entities, and in that mode it already omits a punched key whose punch lies below the
lower bound of the range; with the lower bound set to `S`, it therefore selects exactly the keys
punched inside the migrated range. It also already determines, for every key, the epoch of the
most recent punch at or below the upper bound, because record visibility depends on it. The
change exposes that epoch on the reported entry; it computes nothing new and adds no walk and no
round trip to the pull.

Keys punched and re-created several times need no further treatment: replaying the most recent
punch removes everything the reintegrating target could still hold under the key from before it,
and the values written after the last re-creation are pulled as ordinary records of the range.
Snapshots inside the migrated range are handled as today, by enumerating one range per snapshot;
each range reports its own covering and later punches, so the key's state at every snapshot and
at the present is reproduced.

**Alternative considered.** `S` could be treated as a snapshot, with one additional enumeration
of each object over the range ending at `S`, reporting only the keys visible at `S` that carry a
later punch, using the existing snapshot descriptors. This requires no iterator change. However,
the incremental pull already visits every dkey and akey of an object, because the key trees are
not indexed by epoch, so the additional enumeration would double the key walk and the number of
enumeration round trips for every object, whether or not it has any punch. The single-walk design
is preferred for this reason; the punch-only walk remains available as a fallback should the
iterator extension prove impractical.

##### Retaining Punch Evidence

Discovery is only effective while the punch is still present to be discovered. Retention must
reconcile two requirements. Aggregation must remain free to reclaim the space of punched keys,
otherwise a workload that repeatedly creates and punches keys grows without bound. At the same
time, a punch that occurred after a target's exclusion must remain discoverable until that target
has been reintegrated, or until the system gives up on it.

Neither requirement can be dropped, so the design resolves the conflict in time rather than
absolutely: punch evidence is retained in full while it is affordable, is downgraded to cheaper,
lossier forms when it is not, and is released once no target can need it. This yields three
retention tiers, a fallback path for the objects or targets whose evidence has been downgraded,
and a release mechanism.

##### Tier 1: Retain the Punched Entry and Its Last Punch

In tier 1, aggregation never removes the most recent committed punch entry of an ilog. All other
entries are aggregated as today: the creation that the punch cancels is removed, earlier punches
are removed, and if the key was re-created after the punch, the punch and the later creation
coexist in the log. A key that was punched and not re-created therefore keeps its object, dkey or
akey entry with a one-entry ilog holding the punch, instead of being deleted from its parent
tree. Only the subtree beneath the key is destroyed. The bulk of the space is still reclaimed
immediately; what is retained is one key record and one ilog entry per punched entity.

The punched entry remains enumerable, so reintegration can find the punch and replay it. Tier 1
is the normal operating mode, and in it incremental reintegration is always possible.

Tier 1 is what makes the discovery walk sound. When punched entities are requested, the ilog
visibility rules treat a key whose only remaining entry is a committed punch as a punched key
rather than a nonexistent one, and it is reported as covered with that punch as its covering
punch epoch; a re-created key is reported as visible with the same epoch. The retained punch is
the one fact the walk requires: when the key was last punched. Nothing else about the key's
history needs to survive, because the last punch removes everything a target could hold under the
key from before it, regardless of earlier punches and re-creations. Retaining the most recent
punch is also what makes the re-created case safe: otherwise aggregation would leave only the
re-creation in the log, the walk would report the key as visible with no covering punch, and the
reintegrating target would keep its pre-punch values under the key alongside the new ones.

Retaining the punch entry retains no data. The values under a punched key are reclaimed by
record-level aggregation, which deletes every record covered by the key's punch regardless of the
key's ilog, so the retained entry refers to an empty subtree or, for a re-created key, to a
subtree holding only the values written after the punch.

##### Tier 2: Downgrade to a Per-Object Punch Record

Tier 1 has a cost and cannot be unconditional. When the retained entries can no longer be
afforded, aggregation removes the retained punches as it does today, deleting punched keys
outright where nothing else remains in their logs, but first records that punch evidence was
lost, in a new per-container **punch tree** in the VOS container durable format.

The punch tree is deliberately coarse. It is keyed by object ID only and does not record which
dkey or akey was punched:

| Field | Content |
| --- | --- |
| Tree class | Compact ordered key-value btree in the container durable format |
| Key | `daos_unit_oid_t` of the object whose punch evidence was discarded |
| Value | The *watermark*, a `daos_epoch_t`: the highest epoch of any punch discarded for the object; and a flag word, of which one bit records that a punch at object level was among the discarded ones |

Recording only the highest discarded epoch is sufficient. A reintegrating target with global
stable epoch `S` needs every punch above `S`, so evidence it needs is missing if and only if some
discarded punch had an epoch greater than `S`, which is exactly the condition `watermark > S`.
Updating an existing entry is a maximum on the watermark and a bitwise or on the flags, so an
object never occupies more than one entry, however many times it is punched and aggregated.

The object-level flag is a hint only. It tells the scan which entries may refer to objects that
no longer exist in the object index, so that the lookup described in the fallback below can be
skipped for the others. It does not by itself say that the object is absent: an object punched
and re-created keeps its index entry while its flag is set. No correctness argument depends on
the flag.

Alongside the tree, the extension record keeps the **tree high watermark**: the highest
watermark ever inserted into the current tree. It is maintained in the same transaction as the
insert or update that raises it, with a compare and a single field write, so it is always exact
and durable together with the entries it summarizes. It lets the release of the whole tree
described below be decided without walking the tree, and it is reset when the tree is detached.

The tree requires no durable format version bump. The container durable format already reserves
unused space at the end of its extension block and already carries valid bits describing which
optional extension fields are initialized. The tree root is allocated separately and referenced
from the extension record by offset, so that a tree can be detached in one step and drained
afterwards; a second offset holds a detached tree until its space has been reclaimed. A new
valid bit is added for the tree, and the tree is created lazily the first time an entry is
recorded. Containers written by older versions have the bits clear and behave as today.

Recording the watermark and discarding the punch must be atomic with respect to failure; if the
punch were removed without the watermark being durable, the punch would be lost silently and a
reintegrating target would resurrect stale data. Aggregation therefore performs the punch tree
update and the removal of the punch entry, or of the whole punched key, in one memory
transaction, which is possible because both act on structures in the same VOS container.

Once an object has a punch tree entry, aggregation treats it exactly as today: punched dkeys,
akeys and their subtrees are destroyed outright, and the watermark is raised if a later punch is
discarded. There is no further retention cost for that object.

##### Fallback: Full Migration of the Affected Objects

An object listed in the punch tree cannot be incrementally reintegrated, but the rest of the
container can. The fallback is therefore per object, not per target, and the cost of having left
tier 1 is bounded by the number of objects in the punch tree.

Two paths must consult the punch tree; both are required.

- **Serving the rebuild enumeration.** When an engine enumerates an object for a reintegrating
  target, it checks the punch tree. The enumeration request already carries the lower bound of the
  requested epoch range: for a reintegration pull this is the target's global stable epoch `S`, or
  the lower edge of a snapshot range above it, whereas a full rebuild always requests from epoch
  0. The engine therefore applies the check `watermark > S` only when the requested lower bound is
  non-zero. When the check fires, the enumeration cannot be trusted and the engine returns a
  dedicated error, `-DER_NEED_FULL_REBUILD`, instead of a result. The puller then discards its
  local shard of the object and rebuilds it from epoch 0. Because the check is made on every
  enumeration request, an object that enters the punch tree while its migration is in progress is
  caught by the next request for it.
- **Scanning for objects that no longer exist locally.** If the punched entity was the object
  itself, its entry may be gone from the object index entirely; no enumeration will mention it and
  the check above never fires, leaving the stale object on the reintegrating target. The scan
  phase therefore also iterates the punch tree. For every entry whose watermark is above the
  reintegrating target stable epoch carried in the scan request, and whose object-level flag is
  set, it looks the object up in the object index; if the object is absent, it pushes the object
  to the puller as an explicit discard-and-rebuild instruction. Objects still present in the
  index are left to the regular scan and the enumeration check above.

The two cases the punch tree records, an object some of whose keys were punched and an object
that was punched itself, require no distinction on the puller. Both result in the same action:
the local shard of the object is discarded and the object is pulled from epoch 0. For an object
that still exists on the source this reproduces its current content; for an object that was
punched and not re-created the pull returns nothing, and the object is deleted, which is the
correct final state. The two cases differ only in which path detects the object, and that is
determined from the object index at scan time rather than from the punch tree.

The cost of the fallback differs by object class. For a replicated object a rebuild from epoch 0
is a copy from a surviving replica; for an erasure coded object it may require reading a full
stripe from the data shards and recomputing parity. This asymmetry is a further reason to keep
tier 1 as the normal operating mode and to treat tier 2 as an exceptional state.

##### Tier Transition Policy

The transitions between tiers reuse the space pressure gauge that the engine scheduler already
maintains per pool, rather than introducing separate accounting of retained entries. The levels
of that gauge are defined by the scheduler and are not changed by this design; it rates a pool by
its free space ratio as follows: no pressure above 40% free, level 1 from 30% to 40%, level 2
from 20% to 30%, level 3 from 10% to 20%, level 4 from 5% to 10%, and level 5 below 5%. At each
level the scheduler delays client updates for longer and grants aggregation and garbage
collection a larger share of the CPU. The retention tiers are mapped onto these levels as
follows.

| Free space | Pressure level | Tier |
| --- | --- | --- |
| More than 30% | None, or 1 | Tier 1: punch evidence fully retained |
| 20% to 30% | 2 | Tier 1, or tier 2 if already entered: hysteresis band |
| 10% to 20% | 3 | Tier 2: retained punches reclaimed, per-object watermarks recorded, or tier 3 if already entered |
| Less than 10% | 4 or above | Tier 3: container punch watermark only |

Tier 2 is entered at level 3, where the scheduler already favors space reclamation over client
updates, so metadata retained purely for a possible future reintegration is no longer the best
use of the remaining space. Tier 3, specified below, is entered at level 4, where the scheduler
treats reclamation as urgent. Both levels are exposed as tunables so that they can be adjusted
once real workloads have been measured.

Each tier is left only when pressure has receded below the level at which the tier before it is
entered: tier 2 is left when the gauge reports level 1 or below, tier 3 when it reports level 2
or below. Leaving tier 1 is irreversible for the objects whose evidence has already been discarded,
and leaving tier 2 is irreversible for the objects whose entries were folded into the container
watermark, so a pool hovering around a single threshold must not cross it repeatedly and lose
evidence for a new set of objects each time.

Space pressure decides how much evidence is retained; it does not decide when evidence may be
discarded. That is the subject of the next two subsections: the release of evidence that no
target can need any more, and the bound applied while some target remains excluded indefinitely.

##### Releasing Punch Evidence

**Requirement.** A retained punch entry or punch tree record with epoch `W` on a healthy shard of
container `c` is needed by a target `T` only if `T` is not fully integrated and `W > S_T`, where
`S_T` is the global stable epoch stored on `T` for `c`. While `T` is excluded, `S_T` is known only
to `T`; no healthy target can evaluate the second condition on its behalf, so no release rule may
depend on it. The presence of a `DOWNOUT` target in particular does not permit a release: such a
target can be reintegrated and, in incremental mode, retains its data. A release rule must
therefore establish, from information available to the healthy side alone, that no current or
future excluded target can need the evidence being released.

**Release epoch.** The release is decided by the pool service leader, from the pool map it holds,
and disseminated with the global stable epoch itself. The per-container global stable epoch
broadcast carries a *release* bit. The leader sets the bit on a round for container `c` if and
only if every target in its pool map is `UPIN` and the epoch `S_c` of that round was computed
from the reports of all engines, with none skipped as failed or missing. When the bit is set, the
broadcast epoch is also the *release epoch* `E_c` of the container. On receiving such a
broadcast, each target:

- permits aggregation to remove retained punch entries of `c` whose epoch is below `E_c`, lifting
  the tier 1 rule for those entries; and
- removes punch tree entries of `c` whose watermark is below `E_c`.

Entries at or above `E_c` are retained.

**Execution.** Entries are recorded from punches that were already committed and aggregated, so
`E_c` normally lies above every entry and the whole tree is released. The target compares the
tree high watermark kept in the extension record with `E_c`. If it is below `E_c`, the tree is
detached from the extension record in one transaction and subsequently drained in credit-bounded
transactions on the target's own execution stream, in the same manner as garbage collection
drains the trees of a destroyed container. A detached tree occupies its own slot in the extension
record until drained, so that a crash during the drain loses no space; the drain resumes when the
container is next opened. Only when the tree high watermark is at or above `E_c` does the release
walk the tree and delete the entries below `E_c` in batches. After a release the tree is created
again lazily by the next tier 2 record, and tier 1 retention applies again to every punch at or
above `E_c`, and to all punches as soon as a broadcast without the bit is received. Incremental
reintegration of targets excluded after a release is therefore unaffected by it.

**Correctness.** The release must remain correct under concurrent pool map changes and in-flight
modifications. Let `T` be any target excluded after the leader's check. `T` can lack a punch `P`
only if `P` committed without it, and hence after its exclusion. At the time of the round, every
shard reported a local stable epoch of at least `E_c`, and a local stable epoch is by definition
below the epoch of every prepared and uncommitted modification on the shard. Suppose `P` had an
epoch below `E_c`. Either `P` was already prepared on some shard at report time, in which case
that shard would have reported an epoch below `E_c`, contradicting the definition of `E_c` as the
minimum over all reports; or `P` arrived after the report, in which case it was rejected and
restarted with an epoch above the reported stable epoch, as the global stable epoch section
requires. Every punch that a target excluded after the check can lack therefore has an epoch of
at least `E_c`, and no entry below `E_c` is needed by such a target. Targets excluded before the
check do not exist, by the first condition on the bit. The same argument applies to retained
tier 1 entries below `E_c`.

**Why the decision is not local.** A target's own copy of the pool map is not a safe basis for
release, because it can lag the leader's. A punch may be prepared on a healthy shard while every
target is still `UPIN` in that shard's map and committed after one target's exclusion, without
any pool map refresh being forced on the shard. The shard then aggregates and records the punch
and, under a rule of the form "no excluded target in my map, hence release", would discard the
record while the excluded target still needs it. The release epoch avoids this hazard because its
correctness argument depends on epochs only, not on the releasing target's knowledge of the pool
map.

##### Tier 3: Container-Level Punch Watermark

If an excluded target is never reintegrated, the release bit is never set and the punch tree of
every container can only grow. Its growth is slow, one small entry per punched object, but it
must be bounded. When space pressure reaches level 4, or when a shard's punch tree exceeds a
configurable size cap, the shard downgrades once more. In one transaction it records the tree high
watermark as a single **container punch watermark** in the extension record and detaches the
tree, which is then drained as described above. The tree may be created again afterwards, so the
container punch watermark is the maximum over every tree the shard has dropped.

A shard whose container punch watermark exceeds `S` cannot serve any incremental pull of that
container: objects punched wholesale and reclaimed are no longer enumerable, so no procedure
short of replacing the container shard on the reintegrating target can be shown complete. The
design does not attempt a per-container recovery for this exceptional state. Instead, before
reporting any object, every scanning target compares its container punch watermarks with the
reintegrating target stable epoch carried in the scan request, and if any exceeds it, reports in
its scan status that a full rebuild is required. The leader then aborts the incremental
operation and re-schedules the reintegration of the same targets in full-reintegration mode, in
which local data is discarded as today. For pulls already in flight, the enumeration check of the
fallback also fires when the container punch watermark exceeds the requested lower bound.

**Operating in tier 3.** While the shard is in tier 3, it does not create a new punch tree:
aggregation raises the container punch watermark directly whenever it discards a punch, at
constant cost. The shard returns to tier 2 recording when pressure has receded to level 2 or
below, and to tier 1 when it has receded to level 1 or below, as the tier transition policy
specifies.
A tree dropped because of the size cap is likewise not re-created until the next release has
cleared the container punch watermark. The container punch watermark itself persists until it is
released.

**Resuming incremental mode.** Tier 3 is not a terminal state, and the container punch watermark
disqualifies only targets whose stored stable epoch lies below it. Incremental reintegration is
available again along three paths.

- *Targets excluded later.* The watermark is the epoch of a punch that was already committed and
  aggregated when it was recorded, so the global stable epoch of the container passes it within a
  few broadcast rounds. Any target excluded after that point stores a stable epoch above the
  watermark, and the checks described above never fire for it; it is reintegrated incrementally
  even while the watermark is still recorded.
- *Release.* When a broadcast carries the release bit with a release epoch `E_c` above the
  container punch watermark, the shard clears the watermark. The correctness argument of the
  release applies unchanged: no punch below `E_c` can be needed by any target excluded afterwards.
  A release with `E_c` at or below the watermark leaves it in place until a later round.
- *Recovery of space.* As described above, the shard resumes per-object and then full retention
  as space pressure recedes, so evidence for new punches is again retained in the finer forms.

The targets that do not recover incremental mode for the container are those already excluded
with a stored stable epoch below the watermark. They are reintegrated in full-reintegration mode
once, after which they are `UPIN` and subject to none of the above.

An administrator can also give up on an excluded target explicitly. Changing the pool's
`reintegration` property away from `incremental` releases all punch evidence on the next
broadcast round, irrespective of the pool map; a later reintegration of that target runs in
full-reintegration mode.

### Fast Reintegration Mode

Incremental migration reduces the amount of data moved during reintegration, but it does not
reduce the cost of finding out that there is nothing to move. Even when no modification at all
happened while a target was excluded, every healthy target still walks every object in every
container shard it holds, computes the object's layout to decide whether a shard belongs to a
reintegrating target, and reports the result to the leader. For a large pool this scan phase
dominates the duration of a reintegration that has no data to migrate. Fast reintegration mode
is a shortcut for exactly that case: it lets a healthy target prove, from one value per container
shard, that none of its data can be needed by the reintegrating targets, and skip the scan
entirely. The same idea, applied to a second per-container value, lets targets that received no
rebuilt data skip the reclaim scan that follows the reintegration.

Fast mode is an optimization only. Its preconditions are checked before any scan starts, and when
they do not hold the reintegration proceeds in the regular incremental mode. It never changes the
result of a reintegration, only how quickly one completes when no modification occurred during
the exclusion.

Throughout this section, `S_c` denotes the global stable epoch stored for container `c` on a
reintegrating target, and `M` denotes the *reintegrating target stable epoch*, the single
pool-wide value derived from the `S_c` as described below.

#### Prerequisites

Fast mode is sound only when two conditions hold together.

1. **No modification happened at or above the reintegrating target's stable epoch.** The data a
   reintegrating target holds for container `c` is complete and consistent below `S_c`. If no
   healthy shard of `c` carries a modification with an epoch at or above `S_c`, then every
   modification of `c` that exists anywhere in the pool is already present on the reintegrating
   target, and incremental migration would find nothing to move.
2. **The object layout is unchanged.** The migration lower bound applies only to shards that
   already exist on the reintegrating target; a shard that lands there because of a layout change
   must be migrated in full, which only a scan can discover. The layout after reintegration equals
   the layout before exclusion only if all currently excluded targets are reintegrated in the same
   operation and the pool map has seen no other change that affects placement since the earliest
   of those exclusions: no extension, drain, layout version upgrade or reintegration of some other
   target.

The first condition is decided from data on the healthy targets; the second from the pool map on
the leader.

#### Summary of Changes

| Area | Change |
|---|---|
| VOS container | Persistent *container maximum write epoch*: an upper bound on the epoch of every client-originated modification in the shard. |
| VOS container | Persistent *container maximum migration version*: the highest pool map version of any migration write received by the shard. |
| Container recovery | Each reintegrating target reports the minimum of its stored global stable epochs; the leader reduces them to `M` and keeps it with the pending rebuild operation. Recovery marks the shards it creates. |
| Pool service | Persists the pool map version current at the last object layout version change. |
| Rebuild leader | Evaluates the layout precondition from the pool map and the pool properties; passes `M` and an eligibility flag in the scan request, and the exclusion map version in the reclaim request. |
| Rebuild scan | A scanning target compares its container maximum write epochs against `M` and skips the object walk when all are below it. |
| Rebuild reclaim | A target compares its container maximum migration versions against the exclusion version and skips the object walk when all are below it. |

#### Container Maximum Write Epoch

The first prerequisite requires each healthy target to know the highest epoch of any modification
in each of its container shards. VOS already tracks this per object, as the object's maximum
write epoch, but consulting it would require walking the object index, which is the very cost
fast mode avoids. The design therefore adds a **container maximum write epoch** to the persistent
extension record of each VOS container shard, alongside the global stable epoch stored there
already.

**Semantics.** The value is an *upper bound*, not an exact maximum: it is guaranteed to be at
least the epoch of every client-originated update and punch, at object, dkey, akey or record
level, that has been applied to the shard, and it may exceed the true maximum. This direction of
imprecision is the safe one. An overestimate can only cause a healthy target to fall back to the
regular scan; an underestimate would allow it to skip a scan while holding data the
reintegrating target lacks, which would be a data-loss bug. Any scheme that persists the value
lazily, such as a periodic timer, is ruled out for this reason: after a crash a lagging value
would be lower than modifications that did survive.

**Maintenance.** The upper-bound semantics is also what keeps the maintenance cost negligible.
Every modification already runs inside a VOS transaction that updates the object's maximum write
epoch when the new epoch exceeds it; the same transaction now also compares the epoch against the
container's persisted value. Only when the epoch exceeds the persisted value is the container
field rewritten, and it is set not to the epoch itself but to the epoch plus a fixed *lease* of
about one minute. Every modification in the following minute falls below the lease and costs
nothing beyond the comparison. The persistent write is thus amortized to one small field per
container shard per lease period, regardless of the modification rate, and the imprecision is
bounded by the lease length. An exact in-memory value may be kept alongside for diagnostics, but
the fast-mode decision uses the persisted bound only, so it holds across engine restarts without
any recovery step. The value is never reset or lowered; aggregation, snapshot deletion and object
reclaim do not change it, since they do not introduce data at new epochs.

**Migration writes are excluded.** Writes performed by rebuild, reintegration, extension or drain
to re-materialize a shard from other shards do not maintain the bound. Two facts motivate the
exclusion. First, migration writes do not always carry the original epochs of the data: a rebuilt
parity extent that has to be fetched rather than re-encoded is stamped with the rebuild epoch of
that operation, which is later than every modification made before the exclusion. If such writes
raised the bound, any healthy target that received rebuilt parity while a target was excluded
would carry a bound above the excluded target's stable epoch, and fast mode would be unavailable
in the very sequence it is meant for: exclusion, rebuild, reintegration. Second, the exclusion is
safe: a migration write introduces no content that is not already present, at its original epoch,
on the shards it was derived from, and every one of those original modifications raised the bound
of its own container shard when it was written. The bound is therefore a bound on
*client-originated* modifications, which, as the correctness argument below shows, is exactly what
the fast-mode decision requires. Migration writes are already flagged as such in the VOS update
path, so no new interface is needed to recognize them.

#### Container Maximum Migration Version

Because migration writes are outside the maximum write epoch, fast mode says nothing about the
copies that rebuild placed on healthy targets while the reintegrated targets were excluded. Those
copies still exist after a fast reintegration, no longer belong to the targets that hold them
under the restored layout, and are removed by the reclaim operation that follows every
reintegration. To let reclaim benefit from the same shortcut, each VOS container shard also
records a **container maximum migration version**: the highest pool map version carried by any
migration write it has received.

Epochs are unsuitable for this purpose, since migration writes of replicated data and of EC data
extents carry the original epochs of the data, which may lie below any stable epoch. The pool map
version of the operation that produced a migration write is, in contrast, always at least the
version at which the target being rebuilt was excluded. The value changes at most once per
rebuild operation per container shard, so it is written in the same transaction as the migration
write whenever it increases, with no lease and no amortization needed. Like the maximum write
epoch, it is never lowered.

#### Protocol

The protocol has four steps: collection of `M` during container recovery, evaluation of the
layout precondition by the leader, the per-target scan decision, and the per-target reclaim
decision.

**Collecting the reintegrating target stable epoch.** `M` is collected during container recovery,
the first phase of the reintegration flow, rather than at scan time. Container recovery already
sends one collective request to every joining engine and waits for every reply before the pool
map is updated, and it is the phase in which each reintegrating target walks its container shards
anyway. Each reintegrating target therefore, as part of reconciling its shards, takes the
*minimum* of the `S_c` stored in the container shards it already held before the recovery, and
the engine returns the minimum over its reintegrating targets in its reply to the recovery
request. The leader reduces the replies, again by minimum, into `M`, keeps it with the pending
rebuild operation it schedules for the pool map update that follows, and hands it to the scan
request when that operation starts. Engines with no reintegrating target do not contribute.

The minimum, rather than the maximum, is the value that makes the later comparison valid for
every container at once: a modification is safe to ignore only if it lies below the stable epoch
of the container it belongs to, so a single pool-wide threshold must be no larger than the
smallest of them. Container shards created by the recovery itself carry no meaningful stable
epoch and are left out of the minimum; any data in such a container was written during the
exclusion and therefore raises the container maximum write epoch on the healthy targets, which
disables fast mode by the other side of the comparison.

Collecting the value in the recovery phase has three consequences.

- *The value is final.* A reintegrating target is not `UPIN` during recovery, so it receives no
  stable epoch updates until reintegration completes. The stable epochs read during recovery are
  therefore the ones still in force when the scan starts.
- *The value is held in memory on the leader and re-collected when needed.* It travels from the
  recovery reply into the rebuild operation the leader queues for the pool map update, and is not
  persisted. When a new leader takes over and re-creates the pending rebuild operations from the
  target states in the pool map, the value is not available to it, so before starting a
  re-created reintegration operation the new leader collects it again, with a query-only variant
  of the recovery request that reads the stored stable epochs without reconciling any shard. The
  result is the same as the original collection, because the value is final. Container recovery
  marks the shards it creates so that they are excluded from the minimum in this second
  collection just as they were in the first.
- *Merged requests take the minimum.* The leader may combine several reintegration requests that
  arrive close together into one rebuild operation. Each request brought its own value from its
  own recovery; the combined operation uses the smallest of them, which is the only value valid
  for every target in the combined set.

**Evaluating the layout precondition.** Before broadcasting the scan request, the leader
determines whether the layout precondition holds. Let `V_ex` be the pool map version of the
earliest exclusion among the targets being reintegrated. Two sources are consulted.

- *The pool map.* The set of targets that are not fully integrated must be exactly the set being
  reintegrated, and no component may have been added, excluded, drained or reintegrated at a map
  version later than `V_ex`. Every component of the pool map records the versions at which it was
  added, excluded and reintegrated, so this is a single pass over the map.
- *The object layout version.* An object layout version upgrade changes placement for every
  object in the pool but does not change the pool map, so it cannot be detected from the map
  alone. The pool service therefore persists, next to the object layout version itself, the pool
  map version that was current when the layout version was last changed. The precondition
  requires that recorded version to be strictly below `V_ex`: an upgrade completed before the
  exclusion saw a map version below `V_ex`, since the exclusion itself advanced the map to `V_ex`,
  whereas one completed afterwards saw `V_ex` or later. In addition, no layout upgrade may be
  queued or in progress when the reintegration operation starts. On a pool whose service does not
  yet hold the recorded version, it is initialized to the current map version when the service
  first runs with this design, which is conservative: reintegrations of targets excluded before
  that moment take the regular path.

The leader sets a *fast mode eligible* flag in the scan request accordingly and carries `M` in the
same request, so that every scanning target has both inputs before it starts. `M` is carried
whether or not the flag is set, because the punch fallback of the scan also uses it. Targets
never make this determination themselves; the leader is the single authority on the pool map and
the pool properties, which avoids any inconsistency between engines that hold pool maps of
different versions.

**Deciding whether to scan.** A healthy target that sees the eligibility flag iterates its local
VOS container shards and takes the maximum of their container maximum write epochs. If that
maximum is strictly below `M`, no modification anywhere on this target lies at or above the stable
epoch of any container on the reintegrating targets, and the target reports its scan as completed
without visiting a single object. Otherwise it scans as usual. The decision is made independently
per target and requires no additional communication: a pool in which nothing was modified has
every target skip, while a pool with a few late writes has only the targets that hold them scan.
The reintegrating targets themselves are not scanning targets, so the fact that their own
container shards may hold modifications between `S_c` and the time of exclusion does not affect
the decision. When every target skips, the leader observes all scans completed with no objects to
migrate, and the reintegration finishes through the normal completion path.

**Deciding whether to reclaim.** The reclaim operation that follows the reintegration still runs,
because the rebuilt copies it removes are invisible to the maximum write epoch. The leader passes
in the reclaim request the map version at which the earliest of the reintegrated targets was
excluded. A target whose container shards all have a maximum migration version below that value
received no migration write since the exclusion, holds no rebuilt copy, and skips the reclaim scan
without visiting any object. A target that did receive rebuilt copies scans and removes them as
usual. When rebuild ran during the exclusion, copies are typically spread over many targets and
most of them scan; the shortcut matters most when rebuild was delayed or disabled, or when the
reintegration happened before rebuild had made much progress.

#### Correctness Argument

**Scan.** By construction `M <= S_c` for every container `c` the reintegrating target already
held. A healthy target skips its scan only if every container shard it holds has a maximum write
epoch below `M`, which by the upper-bound property means every client-originated modification on
it has an epoch below `M`, and hence below `S_c` for its container. By the definition of the
global stable epoch, every modification below `S_c` was already persistently applied on the
reintegrating target's shard of `c` when `S_c` was agreed, and no modification below `S_c` can be
introduced afterwards. The skipping target therefore holds nothing for `c` that the reintegrating
target lacks. Because the layout is unchanged, the reintegrating target owns exactly the shards it
owned before exclusion, so no shard needs to be created on it either. The set of objects a scan
would have reported is empty, and skipping the scan produces the same result as performing it.

The argument depends on three properties that the rest of the design provides or that hold in the
current system: the global stable epoch really is a lower bound on all future modifications; the
container maximum write epoch never underestimates a client-originated modification; and no path
other than migration modifies the *logical content* of a healthy shard at an epoch below its
maximum write epoch. The last property is where care is required. Ordinary VOS aggregation only
removes versions that are invisible above the aggregation boundary. EC aggregation is the one path
that writes at old epochs, since it replaces partial stripe replicas with parity at the stripe's
existing epoch; it does so only on parity shards, and it skips any stripe whose peer parity shard
is excluded, so an excluded parity shard never misses a parity write it would otherwise have
received, and an excluded data shard is never written to by aggregation at all. Any future path
that modifies shard content at an old epoch must either raise the container maximum write epoch or
be shown to preserve this property.

**Migration writes.** These need a separate step because they are excluded from the bound and may
be stamped with an epoch later than the data they carry. Consider content that a rebuild placed on
a healthy target `A` while the reintegrating target was excluded. That content was derived from
other healthy shards, where the same logical modifications exist at their original epochs `e_i`.
If any `e_i >= M`, the container shard holding it raised its bound to at least `e_i` when the
modification was written, so that target does not skip and the reintegration takes the regular
path; the fast-mode decision is never reached. If every `e_i < M`, then every such modification
lies below the stable epoch of its container and is already present on the reintegrating target,
so the rebuilt content on `A`, whatever epoch it was stamped with, adds nothing the reintegrating
target lacks. The rebuilt copy itself is removed by the reclaim that follows, exactly as in the
regular path. The excluded target cannot have been a source for those migration writes, and any
other target that served as a source and has since failed or been reintegrated violates the layout
precondition, so the sources whose bounds the argument relies on are always among the targets
that evaluate the fast-mode check.

**Reclaim.** Under the layout precondition, the only shards on a healthy target that do not
belong to it after reintegration are copies written by migration operations that ran during the
exclusion, and every such operation carries a pool map version later than the exclusion. A target
whose container maximum migration versions are all below the exclusion version received no such
write and therefore holds no such copy; skipping its reclaim scan removes nothing that the scan
would have removed.

#### Limitations

- Fast mode is all-or-nothing per target, not per container: a single late write in one container
  makes the target scan every container shard it holds. Per-container thresholds would allow a
  finer decision but would require distributing a list rather than a single epoch; this is left
  as a possible refinement.
- The lease makes the container maximum write epoch imprecise by up to one lease period.
  Modifications in the last minute before an exclusion may therefore disable fast mode for a
  reintegration that would otherwise have qualified. The lease length is a tunable trade-off
  between this imprecision and persistent write amortization. More generally, fast mode requires
  that no client modification of the pool lies at or above `M`, which the stable epoch itself
  trails by the reporting interval and the aggregation gap; fast mode therefore applies to pools
  that were quiescent for some tens of seconds before the exclusion and remained so during it,
  which is the intended target case of a planned or idle-time outage.
- The layout precondition is deliberately strict. Reintegrations that follow an extension, a
  drain, or the earlier reintegration of a different target always take the regular path, even
  when the affected data is small, because proving the layout equivalent in those cases would
  require the very object walk fast mode exists to avoid.
- The reclaim that follows a fast reintegration cannot be skipped as a whole: rebuilt copies of
  the reintegrated targets' shards remain on healthy targets and must be removed. Only targets
  that received no migration write since the exclusion skip the reclaim scan.
- Containers whose stored global stable epoch is zero, for instance on a pool upgraded from a
  release that did not compute it, force `M` to zero and thereby disable fast mode until a stable
  epoch has been agreed and stored on every container.

## Implementation Phases

The design is delivered in four phases. Each phase yields a working system: phase 1 alone
provides incremental reintegration for pools without container changes or punches during the
exclusion, and every later phase removes one restriction or adds one optimization without
altering the on-disk state or protocols introduced by the phases before it.

1. **Phase 1: Global stable epoch and incremental migration.**
   1. Implement the local stable epoch calculation, the IV-based reporting to the PS leader, the
      global stable epoch calculation on the leader, and its broadcast and persistent storage on
      each target.
   2. Change reintegration to enumerate and migrate data starting from the stored global stable
      epoch instead of from epoch 0.
2. **Phase 2: Container recovery process.**
   1. Implement the container list distribution through the `POOL_RECOV_CONT` collective RPC and
      the two-way reconciliation of the container shards on the reintegrating targets.
   2. Implement the `recov_cont` pool property and the retry on a container create or destroy
      that races with recovery.
3. **Phase 3: Object, key and record punch process.**
   1. Extend the scan, the migration enumeration and the puller to discover and replay punches
      inside the migrated range.
   2. Add tier 1 punch retention to VOS aggregation.
   3. Add the punch tree, the tier 2 downgrade and the full-migration fallback of the affected
      objects.
   4. Add the tier 3 container punch watermark, the full rebuild required scan status and the
      re-scheduling of the reintegration in full-reintegration mode.
   5. Implement the tier transition policy driven by the pool space pressure levels, with
      hysteresis.
   6. Add the leader-driven release of punch evidence through the release bit of the stable epoch
      broadcast.
4. **Phase 4: Fast reintegration mode.**
   1. Add the container maximum write epoch and the container maximum migration version to the
      VOS container extension record and maintain them from the update, punch and migration
      paths.
   2. Collect the reintegrating target stable epoch in the container recovery reply, add the
      query-only variant of the recovery RPC, and carry the value in the scan request.
   3. Persist the map version of the last object layout version change in the pool service.
   4. Implement the layout precondition check on the leader and the per-target scan and reclaim
      skips.

## Compatibility and On-Disk Impact

### On-Disk Format Changes

All new persistent state is placed in space that is already reserved in the current durable
formats, so no existing structure is relocated and no data migration is required when a pool is
opened by software that implements this design.

| Structure | Addition | Purpose |
| --- | --- | --- |
| VOS container extension record | Global stable epoch | Lower bound of the migrated range (Global Stable Epoch section) |
| VOS container extension record | Container maximum write epoch, with a validity bit | Fast mode scan decision |
| VOS container extension record | Container maximum migration version, with a validity bit | Fast mode reclaim decision |
| VOS container extension record | Recovery-created marker bit | Exclude shards created by container recovery from the stable epoch minimum |
| VOS container extension record | Punch tree offset, detached tree offset, tree high watermark, container punch watermark | Tier 2 per-object punch watermarks, their release, and the tier 3 downgrade |
| Pool service (RDB) | `recov_cont` property | Detect container create/destroy racing with recovery |
| Pool service (RDB) | Map version of the last object layout version change | Layout precondition of fast mode |

The incremental log (ilog) format is unchanged; tier 1 retention only alters which entries
aggregation is allowed to remove, and every retained entry is a valid entry of the existing
format. The only space effect is that an ilog which would previously have been emptied keeps one
punch entry, and the object, dkey or akey record that owns it is kept alive as a result. The
punch tree occupies space proportional to the number of punched objects recorded under tier 2, is
released when no target can need it any more, and is replaced by a single container watermark
under severe pressure, as described in the punch section.

Incremental reintegration is gated on the pool durable-format version that introduces the
container extension record. Pools created at an earlier version have no extension to hold the
state above; on such pools the stored global stable epoch reads as zero and every reintegration
proceeds in the full-reintegration mode, exactly as today.

### RPC Layout Changes and Interoperability

| Protocol | Change |
| --- | --- |
| Container IV | Two new IV classes: the engine-to-leader stable epoch report, carried with the existing EC aggregation boundary report, and the leader-to-engine global stable epoch broadcast, which also carries the punch evidence release bit |
| Pool service, `POOL_RECOV_CONT` | New collective RPC carrying the authoritative container list by bulk transfer; its reply carries the minimum stored stable epoch of the reintegrating targets; a request flag selects the query-only variant |
| Rebuild scan request | New fields: reintegrating target stable epoch, fast-mode eligibility flag, exclusion map version for reclaim |
| Rebuild scan status | New flag: full rebuild required, raised by a scanning target whose container punch watermark exceeds the reintegrating target stable epoch; the leader re-schedules the operation in full-reintegration mode |
| Object enumeration | New request flag asking for covering punch epochs; new key descriptor kinds carrying an object, dkey or akey covering punch epoch |
| Rebuild puller error path | New return code `-DER_NEED_FULL_REBUILD` from a migration source to request full migration of an object |

Every changed protocol is already versioned, and each change is introduced under a new protocol
version. Within a pool whose engines run mixed software versions the following rules apply:

- The rebuild leader uses the lowest protocol version negotiated across the engines. An engine
  that does not understand the new scan request fields receives a request without them and
  performs a regular scan; an engine that does not report a stable epoch in the recovery reply
  causes the reintegrating target stable epoch to be treated as unknown, which disables fast mode
  for that operation.
- A migration source that does not implement covering punch epochs ignores the request flag and
  returns an enumeration without them. The puller detects the absence and falls back to full
  migration of the object, because punch correctness cannot otherwise be guaranteed.
- The IV classes are additive. An engine that does not implement them neither reports nor
  receives stable epochs; the leader excludes its shards from the calculation as it would a failed
  rank, and its targets keep a stored epoch of zero, which yields full reintegration for them.

In every mixed-version case the system therefore degrades to the current behavior rather than to
an incorrect one.

### Backward Compatibility

- **Old pools on new software.** Handled by the durable-format gate above: no extension record,
  stable epoch zero, full reintegration.
- **New pools on old software.** All additions live in reserved fields and unused validity bits,
  which old software ignores. Retained tier 1 punch entries and punch tree records are valid data
  that old software simply never consults. A downgrade does not corrupt the pool; it only loses
  the ability to reintegrate incrementally until the software is upgraded again.
- **Rolling upgrade.** Fast mode and covering punch epochs are enabled by the protocol
  negotiation described above and become effective once all engines of a pool run the new
  software. The container maximum write epoch validity bit is set by the first client write after
  the upgrade; until then a shard reads as "unknown" and forces the regular scan, which is the
  conservative choice.
- **Pool property.** Incremental reintegration is selected per pool through the existing
  `reintegration` pool property value `incremental`. Pools that keep the default value are
  unaffected by this design.

## External Interfaces

No client-visible API changes. The externally visible surface is administrative:

- **Pool property `reintegration`.** The value `incremental` selects the mode described in this
  document. The existing values retain their meaning. The property may be changed at any time; it
  is evaluated when a reintegration is started. Changing it away from `incremental` also releases
  all retained punch evidence on the next stable epoch broadcast round.
- **Reintegration commands.** `dmg pool reintegrate` and the automatic reintegration triggered by
  system self-healing are unchanged in syntax and semantics.
- **Rebuild status.** The rebuild status returned by pool query is extended with an indication of
  the mode in which the current or last reintegration ran (full, incremental or fast) and, for
  incremental reintegration, the number of objects that were migrated in full because of the
  punch fallback. Both are informational.
- **Server tunables.** The lease length of the container maximum write epoch, the space pressure
  levels at which the punch retention policy enters tier 2 and tier 3, and the punch tree size
  cap are exposed as server-side tunables with the defaults given in this document. They do not
  need to be changed in normal operation.
- **Telemetry.** New engine metrics count, per pool, the targets that skipped the scan and the
  reclaim scan in fast mode, the objects migrated in full because of the punch fallback, and the
  transitions between punch retention tiers. Existing rebuild metrics apply unchanged.
- **Logging.** The rebuild leader logs, at the start of each reintegration, the mode selected and,
  when fast mode was not eligible, the reason. Each scanning target logs whether it skipped its
  scan.

## Testing and Validation

### Unit Tests

VOS:

- Local stable epoch derivation from DTX state, including the boundary-raising side effect: a
  modification with an epoch below a reported stable epoch is rejected and its transaction is
  restarted.
- Global stable epoch accessors: monotonicity, rejection of values above the local stable epoch,
  behavior on containers without an extension record, persistence across container reopen.
- Container maximum write epoch: bound raised with the lease on the first write, unchanged
  within the lease, raised again beyond it; raised by object, dkey, akey and record punches;
  unchanged by aggregation, object deletion and migration writes; persistence and the "unknown"
  reading when the validity bit is clear.
- Container maximum migration version: raised only by migration writes, to their map version.
- Iterator covering punch epochs: reported for keys punched inside the range at every level,
  reported with the same epoch for keys re-created after the punch, not reported for punches
  below the lower bound of the range, correct interaction with snapshots inside the range.
- Aggregation tier 1: the most recent committed punch of an ilog is never removed, including the
  create-punch-re-create sequence inside one aggregation window; everything else is aggregated
  as before.
- Punch tree: insertion, watermark and flag update, release by epoch including the whole-tree
  detach-and-drain path and its resumption after a simulated crash, tier 3 downgrade to the
  container punch watermark, and space accounting.

Rebuild and pool service:

- Layout precondition evaluation over synthesized pool maps: single exclusion and
  reintegration, partial reintegration, extension, drain, reintegration of another target,
  completed and in-progress layout version upgrade.
- Minimum reduction of stable epochs across recovery replies, including engines without
  reintegrating targets and engines running old software.
- Container recovery reconciliation over synthesized container lists: creation of missing
  shards, destruction of orphans, marker on created shards, race detection through the
  `recov_cont` property.
- Puller handling of covering punch epochs: replay order relative to records, replay at the
  correct level, handling of `-DER_NEED_FULL_REBUILD`.

### Integration Tests

Functional tests run on a multi-engine system with the `reintegration` property set to
`incremental`, each verifying data integrity with a full read-back and checksum comparison, and
the container namespace against the pool service:

- **Baseline.** Exclude, reintegrate with no intervening I/O; verify that the scan is skipped on
  every target and that reintegration completes in seconds independently of the data volume.
- **Incremental data.** Exclude, write and overwrite a known fraction of the data, reintegrate;
  verify that the migrated volume is proportional to the data written, not to the data held.
- **Containers.** Create and destroy containers during the exclusion; verify both are reflected
  on the reintegrated target and that I/O to a container created during the exclusion succeeds
  on it.
- **Punches.** Punch objects, dkeys, akeys, single values and array extents during the
  exclusion, including punch followed by re-creation, with and without snapshots inside the
  window, with aggregation running; verify that the reintegrated target reports the same
  visibility as the healthy shards.
- **Space pressure.** Fill the pool past the tier 2 threshold during the exclusion; verify the
  transition, the fallback to full migration of the affected objects, the hysteresis on the way
  back, and the release of retention after the target is reintegrated and the release bit is
  set. Fill past the tier 3 threshold; verify the container punch watermark, the full rebuild
  required status and the re-scheduling of the reintegration in full-reintegration mode. After
  the full-mode reintegration, exclude and reintegrate a target again; verify that the container
  punch watermark no longer disqualifies it and that incremental mode resumes.
- **Punch evidence release.** Punch objects at object and key level during an exclusion, let
  aggregation run in tier 1 and in tier 2, reintegrate, verify that the evidence is released on
  the next broadcast round and that a subsequent exclusion, punch and reintegration cycle is
  again handled incrementally.
- **Fast mode preconditions.** Exclude two targets and reintegrate one; extend or drain during
  the exclusion; upgrade the object layout version during the exclusion; write within the lease
  period before the exclusion. Each must take the regular path and produce a correct result.
- **Rebuild during exclusion.** Exclude with rebuild enabled and with rebuild delayed; verify
  that reintegration takes the fast path when no client I/O occurred, that reclaim removes the
  rebuilt copies, and that reclaim is skipped on targets that received none.
- **Failures in flight.** Kill and restart the pool service leader during recovery, during the
  scan and during the pull; restart a healthy engine and a reintegrating engine at each phase;
  verify convergence to a correct state and that a leader change does not lose fast mode.
- **Races.** Create or destroy a container concurrently with recovery; verify the retry and the
  final state.
- **Mixed versions.** One engine running the previous release; verify the degradation rules of
  the interoperability section.
- **Erasure coding.** Every scenario above repeated with EC object classes, with EC aggregation
  running before, during and after the exclusion.

### Performance Tests

- **Reintegration time versus data volume.** For a fixed per-target capacity, measure the
  reintegration time as a function of the fraction of data written during the exclusion, from
  zero to one hundred percent, for both incremental and full modes. The expected result is a
  near-constant time for fast mode, a time proportional to the written fraction for incremental
  mode, and the current full-mode time as the upper bound.
- **Scan phase cost.** Measure the scan phase alone, with and without fast mode, for pools with a
  large number of small objects, where layout computation dominates.
- **I/O path overhead.** Measure small-update and punch throughput and latency with the container
  maximum write epoch maintenance enabled and disabled. The expected difference is within
  measurement noise, since the added work is a comparison per update and a persistent write per
  container shard per lease period.
- **Aggregation overhead.** Measure aggregation throughput and reclaimed space with tier 1
  retention enabled for workloads with a high punch rate, and the space held by the punch tree
  under tier 2.
- **Reporting overhead.** Confirm that the stable epoch reporting adds no measurable load to the
  pool service leader for pools with many containers, since it rides on an existing report.

## Risks, Mitigations and Future Work

### Risks and Mitigations

| Risk | Mitigation |
| --- | --- |
| A path that modifies shard content at an epoch below the container maximum write epoch without raising it would make fast mode skip a needed scan | The correctness argument identifies EC aggregation as the only such path today and shows it is safe; the invariant is documented at the VOS update entry points and covered by unit tests; any new path must raise the bound or be shown to preserve the property |
| The stable epoch reported by a shard is later than its true stable point, because of a defect in the local stable epoch derivation | The boundary-raising side effect makes a reported epoch self-enforcing: later modifications below it are rejected rather than applied, so the failure mode is a rejected write, not silent divergence |
| Punch evidence is aggregated away before an object can be reintegrated | Tier 1 retention keeps the last punch by default; tier 2 records watermarks when space is short; the fallback migrates the object in full; the object is never left with stale content |
| Space consumed by retained punches or the punch tree under sustained punch-heavy workloads | Retention is bounded by the tier policy; evidence is released by the leader-driven release epoch when no target can need it; the tree is replaced by a single container watermark under severe pressure; metrics expose the space held |
| Punch evidence released while a target still needs it, because of a stale pool map on the releasing target | The release is decided by the leader and carried as an epoch with the stable epoch broadcast; its safety argument depends on epochs only, not on the releasing target's view of the pool map |
| Container recovery races with container create or destroy | The `recov_cont` property detects the race and the operation is retried from the beginning; the per-pool lock prevents interleaving on an engine |
| Leader change during reintegration loses in-memory state | Every in-memory value is either re-collectable (reintegrating target stable epoch) or regenerated from persistent state (rebuild operations from the pool map) |
| Layout change undetected by the fast-mode precondition | Both the pool map and the persisted layout version change record are checked; the precondition is deliberately strict and any doubt selects the regular path |
| Uncommitted data above the stable epoch on the reintegrating target conflicts with migrated data | Such data is not trusted; the migrated range overwrites or removes it, and DTX resync runs before the scan as it does today |
| Increase of reintegration duration in the worst case, when almost everything was rewritten | Bounded by the full-mode time; the design adds only a container walk and a per-object range lookup on top of what full mode already does |

### Future Work

- **Per-container fast-mode decision.** Distribute per-container stable epochs instead of one
  minimum, so that a late write in one container does not force a scan of every container shard
  on a target.
- **Original epochs for fetched parity.** Stamping rebuilt parity with the original stripe epoch
  rather than the rebuild epoch would let regular incremental reintegration skip rebuilt copies
  that the reintegrating target already holds equivalent content for.
- **Narrowing the stable window.** Reducing the distance between a shard's local stable epoch and
  the global stable epoch, for instance with a higher reporting frequency near an exclusion,
  reduces the data re-migrated unnecessarily.
- **Incremental paths for other operations.** Drain and extension could use the covering punch
  epochs and the migration version tracking, although the layout change makes a stable epoch
  lower bound inapplicable to them as designed here.
- **Persisting the reintegrating target stable epoch** with the pending rebuild operation, to
  remove the query round trip after a leader change.
- **Adaptive lease.** Deriving the lease of the container maximum write epoch from the observed
  write rate, to shorten the imprecision window on lightly written containers.

## Appendix: Alternatives Considered

- **Change journal on the excluded target's peers.** Recording, on every healthy shard, the
  keys modified while a peer is excluded, and replaying the journal on reintegration. Rejected:
  it adds a write to every I/O in the common healthy case, must be bounded and spilled under
  sustained writes, and duplicates information that the epoch-indexed VOS trees already hold.
  The stable epoch approach costs nothing on the I/O path and reuses the existing range
  enumeration.
- **Per-target local stable epoch only, without a global agreement.** Using the excluded target's
  own local stable epoch as the migration lower bound. Rejected: the local epoch of one shard says
  nothing about whether a modification below it was applied on other shards of the same
  container, so it is not a valid lower bound for data that must be pulled from them.
- **Dedicated punch-only enumeration.** Discovering punches with an additional enumeration of
  each object over the migrated range, in a punch-only mode. Rejected in favor of extending the
  iterator to report covering punch epochs in the enumeration that migration performs anyway;
  the alternative doubled the enumeration cost per object for information the iterator already
  computes. The punch-only mode remains available as a fallback should the iterator extension
  prove unworkable for a particular tree type.
- **Retaining every punch during exclusion.** Suspending aggregation of punches entirely while
  any target is excluded. Rejected: an exclusion may last indefinitely and space reclamation is a
  first-class requirement; the tiered policy retains only the last punch by default and degrades
  to watermarks under pressure.
- **Releasing punch evidence from the target's own pool map.** Letting each target discard its
  retained punches and punch tree when its copy of the pool map shows no excluded target.
  Rejected: the copy can lag the leader's, and a punch committed after an exclusion the target
  has not yet learned of could be discarded while still needed. The leader-decided release epoch
  carried with the stable epoch broadcast has a correctness argument that depends on epochs only.
- **Per-container recovery after a tier 3 downgrade.** Wiping and rebuilding only the affected
  container shard on the reintegrating target. Rejected for the tech preview: it requires the
  scan to report every object of one container from epoch 0 while the others are handled
  incrementally, and the puller to wipe the shard before any pulled data for it arrives;
  re-scheduling the whole reintegration in full mode is simpler and the state is exceptional.
- **Maximum instead of minimum of the stored stable epochs** for the fast-mode threshold.
  Rejected: a container with a lower stable epoch may have modifications between its epoch and
  the maximum, which the comparison against the maximum would fail to detect.
- **Lazily persisted container maximum write epoch.** Persisting an exact value on a timer
  instead of an upper bound in the write transaction. Rejected: after a crash the persisted
  value lags the surviving modifications, and fast mode would skip a needed scan. The lease
  scheme achieves the same amortization with the imprecision in the safe direction.
- **Collecting the reintegrating target stable epoch in the scan request.** Rejected in favor of
  the container recovery phase, which already exchanges one collective request with every
  joining engine before the pool map is updated and already walks the shards concerned; the
  scan-time collection would have added a dependency between the scan reply and the scan itself.
- **Per-object maximum write epoch for fast mode.** Using the existing per-object value directly.
  Rejected: it requires walking the object index, which is the cost fast mode exists to remove.
