# Incremental Reintegration (Preview) Design

## Summary

- **Problem**:
  When a storage system encounters a transient recoverable failure, the common solution is to
  temporarily exclude it from the system and reintegrate it back in a short time after it is
  repaired. In that process, the current full-reintegration mode fails to meet the requirements
  for performance and usability. This design refactors the protocol to support incremental
  reintegration, which avoids moving the majority of the existing old data and only migrates the
  incremental, newly generated data, while guaranteeing data integrity.

- **Proposal**:
  Maintain a globally consistent stable epoch - the data below that epoch is consistently
  committed or aborted. Store that epoch locally before engine exclusion, and only migrate data
  of a higher epoch during reintegration.

  Existing old containers can be destroyed and new containers can be created after exclusion. To
  keep container existence consistent, a container recovery process must be run as the first step
  of reintegration.

  Objects, keys (dkey and/or akey) and records can be punched after exclusion, and those punch
  records may be removed during normal VOS aggregation. Special handling is designed to make it
  possible to find and replay the punch during reintegration.

  In the typical transient failure scenario where there have been no updates since the failure
  and exclusion, a fast mode is designed that recognizes the case and refines the scan phase
  process, so that the system can be reintegrated and recovered very quickly.

- **Impact**:
  Reintegration will be much faster in most cases, especially when only a small portion of the
  data, or no data at all, is updated after exclusion. This makes system recovery easier and
  quicker for typical transient failure scenarios.

## Background

In the current version of DAOS, when an engine is excluded and then reintegrated, it wipes out all
the existing data and always reconstructs it from scratch. That mechanism is called
full-reintegration mode. This can take a very long time if the original storage target is nearly
full, because the time consumed equals (capacity / bandwidth). If the failure is a temporary
outage, such as an accidental power down or a daemon crash, wiping out the data before
reintegration generates a lot of unnecessary data movement and makes the reintegration slow.

Container existence is also handled without any special process: all containers are first
destroyed, and then, during reintegration, VOS containers are created as data is migrated back.
Such a mechanism has limitations. For example, a newly created container will not be re-created if
no data needs to be migrated to the reintegrating engine, which can cause subsequent I/O failures.

In order to improve on and address these limitations, "incremental reintegration" is designed for
the recovery system of DAOS: committed data should be mostly retained, and the data updated after
exclusion should be reconstructed by the recovery system. Container, object and key existence
consistency is ensured.

## Goals and Non-Goals

### Goals

1. The majority of the data should be retained for reintegration; it is acceptable to sacrifice a
   small portion of the data that was generated recently, in order to simplify the protocol.
2. Container existence should be consistent between the reintegrated engines and the whole system,
   considering that old containers can be destroyed and new containers can be created after engine
   exclusion.
3. Object, dkey and akey existence consistency needs to be kept, since old ones can be punched and
   new ones can be created after engine exclusion.
4. Data correctness verification after reintegration should be ensured, including the case where
   some old records are punched and new records are written or overwritten.
5. On the performance side, it should be possible to observe a reduced reintegration time compared
   to full-reintegration mode, especially when no data, or only a small portion of the data, is
   changed after exclusion.

### Non-Goals

- No full dataset wipe-out during reintegration.

## Design

### Global Stable Epoch and Incremental Reintegration from It

The foundation of incremental reintegration is a new per-container, system-wide *global stable
epoch*: an epoch below which every modification is guaranteed to be persistently and consistently
applied on all healthy shards of the container. Data below that epoch will not need to be
migrated back when a target rejoins the system, so only the incremental updates above it have to
be reconstructed.

The epoch will be computed bottom-up in three steps (per VOS container shard, per engine, then
per pool service (PS) leader), and then pushed back down and persistently stored on each target.
Wherever possible the existing EC aggregation epoch reporting infrastructure will be extended
instead of introducing new RPCs or new ULTs.

#### Local Stable Epoch (Per VOS Container Shard)

A new VOS API, `vos_cont_get_local_stable_epoch()`, will derive a *local stable epoch* for a
container shard from its DTX status. It shall return approximately the highest epoch below which
all DTX entries are already either committed or aborted, so that no in-flight transaction can
still change the data below it. The estimate has to be bounded by the oldest entry of the sorted,
unsorted and re-indexed DTX lists, and by the aggregation gap `vos_agg_gap`.

Querying the local stable epoch cannot be a pure read: to make the returned epoch genuinely
*stable*, the call must also raise the container's acceptable modification boundary. Once the
boundary is raised, the object I/O layer shall reject any further modification with an older
epoch and restart the corresponding DTX with a newer one. This is the property the whole design
relies on: once an epoch has been reported, the data below it can no longer change.

To avoid adding yet another periodic ULT, the query will be driven by the existing per-container
VOS aggregation ULT, which will cache the result so that the reporting path can consume it
without issuing extra queries.

#### Engine-Wide Reporting to the PS Leader

The existing per-pool epoch reporting ULT, which today reports the EC aggregation boundary every
few seconds, will be extended to also report the stable epoch. For every container the engine
hosts, it shall scan the local container shards, skip the failed ones (targets in `DOWN` or
`DOWNOUT` state) and take the minimum local stable epoch among the remaining healthy shards.

That engine-wide minimum will be sent to the PS leader through a new IV class,
`IV_CONT_TRACK_EPOCH_REPORT`, carried in the same message as the EC aggregation boundary epoch.
Reusing the existing reporting path keeps the additional network and CPU cost of the design
close to zero.

#### Global Stable Epoch Calculation on the PS Leader

On the PS leader engine, the per-container-service epoch tracking ULT will be extended to
maintain the stable epoch alongside the EC aggregation boundary. On each round it shall walk the
epochs reported by all engines, ignore the ranks known to have failed, and pick the *system-wide
minimum* local stable epoch as the new global stable epoch of the container.

Because the value is a minimum over all healthy shards, it is by construction an epoch below
which every shard has already stabilized its data. The leader will then broadcast it to all
engines in the system through a new IV class, `IV_CONT_TRACK_EPOCH`.

#### Persisting the Global Stable Epoch on Each Target

On receiving the broadcast, each engine shall dispatch the new epoch to all of its local targets
and store it persistently, so that it survives the crash or power loss that caused the exclusion
in the first place. A new field, `ced_global_stable_epoch`, will be added to the VOS container
durable-format extension for this purpose, together with the accessors
`vos_cont_set_global_stable_epoch()` and `vos_cont_get_global_stable_epoch()`. The value will
also be cached in memory to keep the reintegration path cheap.

Two rules must guard the update:

- **Only `UPIN` targets may be updated.** A target that is not in `UPIN` state did not
  participate in the calculation of the new global stable epoch, so applying it there would be
  unsafe.
- **The epoch must never roll back and must never exceed the local stable epoch.** The setter
  shall reject an epoch that is older than the currently stored one or newer than the shard's own
  local stable epoch with `-DER_NO_PERM`.

Because the durable format changes, backward compatibility has to be handled explicitly:
containers created before the new pool durable-format version have no extension to store the
epoch in. For those, the getter shall return 0, which naturally degrades to the existing full
reintegration behavior and lets an upgraded system keep working with old pools.

#### Using the Global Stable Epoch During Reintegration

Once an engine or a target is excluded, it stops being `UPIN` and therefore stops receiving epoch
updates, so its stored `ced_global_stable_epoch` will be frozen at the last value agreed upon
while it was still healthy. That frozen value is precisely the point from which the target's data
may have diverged from the rest of the system.

The object migration path will therefore be changed to query the stored global stable epoch and
use it as the lower bound of the migration epoch range, instead of always starting from epoch 0:

- If the object already exists on the local target, only the range above the global stable epoch
  is migrated. Snapshots older than the stable epoch shall be skipped, and a recorded object
  punch shall be replayed only if it happened above the stable epoch.
- If the object does not exist locally at all (it was created after the stable epoch, or it
  landed on this target because of co-location), the lower bound falls back to 0 and the whole
  object is migrated.

With this approach the majority of the data, which is already committed below the global stable
epoch, will be retained in place, and only the incrementally updated data above it has to be
moved back. This is what makes reintegration incremental.

### Container Recovery Process

Because incremental reintegration will no longer wipe out the local data, the VOS container
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

To close this gap, a new container recovery step, `pool_recov_cont()`, will be introduced and
executed as the very first step of reintegration, before the pool map is updated and before any
data migration starts.

#### Placement in the Reintegration Flow

Container recovery will be hooked into the common preparation path shared by the reintegration
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

On the PS leader, the recovery step will build the reference view of the pool:

- The target address list is de-duplicated into a rank list, because reintegrating several
  targets of the same rank requires only one RPC per engine.
- The container list is enumerated excluding the containers whose destroy is already in progress,
  so that such containers are treated as non-existent and their shards will be removed rather
  than recreated.
- The resulting container list is exported through a read-only bulk handle and sent with a new
  `POOL_RECOV_CONT` collective RPC to all the joining ranks. Bulk transfer is chosen over inline
  data so that the RPC stays small even for pools with a large number of containers.

#### Reconciliation on the Reintegrating Target

The target-side handler will access the bulk buffer and run the reconciliation on every local
target in parallel. Each target shall perform a two-way reconciliation:

1. **Filter.** The target first checks that it is actually a member of the recovery list; healthy
   targets of the same engine skip the work.
2. **Create the missing shards.** For every container in the list received from the PS leader,
   the local VOS container shard is created if it does not exist yet.
3. **Destroy the orphan shards.** The list is loaded into a temporary in-memory tree keyed by
   container UUID, and the local containers are enumerated. Any shard that is not found in the
   tree is an orphan and is destroyed, and the enumeration is told to drop the corresponding
   entry.

Once all targets are reconciled, the engine shall also clean up its IV cache, so that no stale
container metadata cached before the exclusion can be reused after the target rejoins.

#### Races Against Concurrent Container Create and Destroy

Container recovery cannot be made atomic with respect to the rest of the system: a container may
be created or destroyed while the `POOL_RECOV_CONT` RPC is in flight, which would silently make
the distributed list obsolete. Two mechanisms are proposed to protect against this.

**A `recov_cont` pool property.** Before sending the RPC, the PS leader will set a new
`recov_cont` RDB property. The container create and destroy paths will reset it within their own
RDB transaction, so that any such operation leaves a durable trace. When the reintegration later
updates the pool map, it re-checks the property: if it has been reset (or removed) in the
meantime, a container create or destroy raced with the recovery and the whole operation fails
with `-DER_AGAIN`. The control plane will map this to a retryable status, and the reintegration
will be restarted from the beginning with a fresh container list.

**A per-pool recovery lock.** The PS leader role may switch while the leader is waiting for
container recovery to complete. The new leader may mark the target `UP` earlier than the old one,
after which container target create/destroy RPCs start arriving at a target that is still
executing a now-stale recovery. To serialize these, a per-pool read-write lock will be taken in
write mode around the recovery collective, and in read mode by the discard path, so that
recovery, discard and normal container operations cannot interleave on the same engine.

With this process, the container namespace of a reintegrated target is guaranteed to match the
authoritative view of the pool service before any data is migrated, which satisfies the container
existence consistency goal.

### Object, Key and Record Punch Process

### Fast Reintegration Mode

## Implementation Phases

The design will be delivered in the following phases:

1. **Phase 1: Global stable epoch reporting and synchronization.** Implement the local stable
   epoch calculation, the IV-based reporting to the PS leader, the global stable epoch
   calculation on the leader, and its broadcast and persistent storage on each target.
2. **Phase 2: Incremental migration from the stable epoch.** Change reintegration to enumerate
   and migrate data starting from the stored global stable epoch instead of from epoch 0.
3. **Phase 3: Container recovery process.** Implement the container list distribution and the
   two-way reconciliation of the container shards on the reintegrating targets.
4. **Phase 4: Object, key and record punch process.**
5. **Phase 5: Fast reintegration mode.**

## Compatibility and On-Disk Impact

### On-Disk Format Changes

### RPC Layout Changes and Interoperability

### Backward Compatibility

## External Interfaces

## Testing and Validation

### Unit Tests

### Integration Tests

### Performance Tests

## Risks, Mitigations and Future Work

### Risks and Mitigations

### Future Work

## Appendix: Alternatives Considered
