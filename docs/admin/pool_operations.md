# Pool Operations

A DAOS pool is a storage reservation that can span any number of storage engines in a
DAOS system. Pools are managed by the administrator. The amount of space allocated
to a pool is decided at creation time with the `dmg pool create` command.
Pools can be expanded at a later time with the `dmg pool expand` command
that adds additional engine ranks to the existing pool's storage allocation.
The DAOS management API also provides these capabilities.


## Pool Basics

The `dmg pool` command is the main administrative tool to manage pools.
Its subcommands can be grouped into the following areas:

* Commands to create, list, query, extend, and destroy a pool.
  These are the basic functions to manage the storage allocation in a pool.

* Commands to list and set pool properties,
  and commands to set and list Access Control Lists (ACLs) for a pool.

* Commands to manage failures and other non-standard scenarios.
  This includes draining, excluding and re-integrating targets,
  and evicting client connections to a pool.

* An upgrade command to upgrade a pool's format version
  after a DAOS software upgrade.

### Creating a Pool

A DAOS pool can be created through the `dmg pool create` command.
The mandatory parameters that are needed for the creation of a pool
are the pool label, and a specification of the size of the storage
allocation.

The pool label must consist of alphanumeric characters, colon (`:`),
period (`.`), hyphen (`-`) or underscore (`_`).
The maximum length of a pool label is 127 characters.
Labels that can be parsed as a UUID (e.g. 123e4567-e89b-12d3-a456-426614174000)
are forbidden. Pool labels must be unique across the DAOS system.

A pool's size is determined by two factors: How many storage engines are
participating in the storage allocation, and how much capacity in each
storage tier is allocated (the latter can be specified either on a per-engine
basis, or as the total for the pool across all participating engines).
The same amount of storage will be allocated on each of the participating
storage engines. If one or more of those engines do not have sufficient free
space for the requested capacity, the pool creation will fail.

If neither the `--nranks` nor the `--ranks` option is used,
then the pool will span all storage engines of the DAOS system.
To limit the pool to only a subset of the engines, those two options
can be used to specify either the desired number of engines,
or an explicit list of engine ranks to be used for this pool.

The capacity of the pool can be specified in three different ways:

1. The `--size` option can be used to specify the _total_ pool
   capacity in **Bytes**. This value denotes the sum of the SCM
   and NVMe capacities. The relative contributions of the SCM and
   NVMe storage tiers to the total pool size are determined by the
   `--tier-ratio` parameter.
   By default this ratio is `6,94`, so for a pool of size 100TB
   there will be 6TB of SCM and 94 TB of NVMe storage.
   An SCM-only pool can be created by using `--tier-ratio 100,0`.

2. The `--size` option can be used to specify the _total_ pool
   capacity as a **percentage of the currently free capacity**.
   In this case, the tier ratio will be ignored. For example,
   requesting `--size=100%` will allocate 100% of the free SCM
   capacity and 100% of the free NVMe capacity to the pool,
   regardless of the ratio of those two free capacity values.

   * This implies that it is not possible to create an SCM-only
     pool by using a percentage size (unless there is no NVMe
     storage in the system at all, and all pools are SCM-only).

   * If the amount of free space is different across the
     participating engines, then the _minimum_ free space is
     used to calculate the space that is allocated per engine.

   * Because the percentage numbers refer to currently free
     space and not total space, the absolute size of a pool
     created with `--size=percentage%` will be impacted by other
     concurrent pool create operations. The command output will
     always list the total capacities in addition to the
     requested percentage.

3. The `--scm-size` parameter (and optionally `--nvme-size`) can
   be used to specify the SCM capacity (and optionally the NVMe
   capacity) _per storage engine_ in **Bytes**.
   The minimum SCM size is 16 MiB per **target**, so for a storage
   engine with 16 targets the minimum is `--scm-size=256MiB`.
   The NVMe size can be zero. If it is non-zero then the minimum
   NVMe size is 1 GiB per **target**, so for a storage engine
   with 16 targets the minimum non-zero NVMe size is
   `--nvme-size=16GiB`.
   To derive the total pool capacity, these per-engine capacities
   have to be multiplied by the number of participating engines.

!!! note
    The suffixes "M", "MB", "G", "GB", "T" or "TB" denote base-10
    capacities, whereas "MiB", "GiB" or "TiB" denote base-2.
    So in the first example above, specifying `--scm-size=256GB`
    would fail as 256 GB is smaller than the minimum 256 GiB.

!!! warning
    Concurrent creation of pools using **size percentage** could lead to
    `ENOSPACE` errors.  Indeed, these operations are not atomic and the overall
    available size retrieved in the first step could be different from the size
    actually available when the second step will be performed (i.e. allocation
    of space for the pool).

Examples:

To create a pool labeled `tank`:
```bash
$ dmg pool create --size=${N}TB tank
```

This command creates a pool labeled `tank` distributed across the DAOS servers
with a target size on each server that is comprised of $N * 0.94 TB of NVMe storage
and $N * 0.06 TB of SCM storage. The default SCM:NVMe ratio
may be adjusted at pool creation time as described above.

The UUID allocated to the newly created pool is printed to stdout
as well as the pool service replica ranks.

```bash
$ dmg pool create --help
...

[create command options]
      -g, --group=      DAOS pool to be owned by given group, format name@domain
      -u, --user=       DAOS pool to be owned by given user, format name@domain
      -p, --label=      Unique label for pool (deprecated, use positional argument)
      -P, --properties= Pool properties to be set
      -a, --acl-file=   Access Control List file path for DAOS pool
      -z, --size=       Total size of DAOS pool (auto)
      -t, --tier-ratio= Percentage of storage tiers for pool storage (auto) (default: 6% SCM, 94% NVMe)
      -k, --nranks=     Number of ranks to use (auto)
      -v, --nsvc=       Number of pool service replicas
      -s, --scm-size=   Per-engine SCM allocation for DAOS pool (manual)
      -n, --nvme-size=  Per-engine NVMe allocation for DAOS pool (manual)
      -r, --ranks=      Storage engine unique identifiers (ranks) for DAOS pool
```

The typical output of this command is as follows:

```bash
$ dmg pool create --size 50GB tank
Creating DAOS pool with automatic storage allocation: 50 GB total, 6,94 tier ratio
Pool created with 6.00% SCM/NVMe ratio
-----------------------------------------
  UUID                 : 8a05bf3a-a088-4a77-bb9f-df989fce7cc8
  Service Ranks        : [1-5]
  Storage Ranks        : [0-15]
  Total Size           : 50 GB
  Storage tier 0 (SCM) : 3.0 GB (188 MB / rank)
  Storage tier 1 (NVMe): 47 GB (3.0 GB / rank)
```

This created a pool with UUID 8a05bf3a-a088-4a77-bb9f-df989fce7cc8,
with pool service redundancy enabled by default (pool service replicas on ranks
1-5). If no redundancy is desired, use `--properties=svc_rf:0` to set the pool
service redundancy property to 0 (or `--nsvc=1`).

Note that it is difficult to determine the usable space by the user, and
currently we cannot provide the precise value. The usable space depends not only
on pool size, but also on number of targets, target size, object class,
storage redundancy factor, etc.


### Listing Pools

To see a list of the pools in the DAOS system:

```bash
$ dmg pool list
Pool     Size   Used Imbalance Disabled
----     ----   ---- --------- --------
tank     47 GB  0%   0%        0/32
```

This returns a table of pool labels (or UUIDs if no label was specified)
with the following information for each pool:
- the total pool size
- the percentage of used space (i.e., 100 * used space  / total space)
- the imbalance percentage indicating whether data distribution across
  the difference storage targets is well balanced. 0% means that there is
  no imbalance and 100% means that out-of-space errors might be returned
  by some storage targets while space is still available on others.
- the number of disabled targets (0 here) and the number of targets that
  the pool was originally configured with (total).

The --verbose option provides more detailed information including the
number of service replicas, the full UUIDs and space distribution
between SCM and NVMe for each pool:

```bash
$ dmg pool list --verbose
Label UUID                                 SvcReps SCM Size SCM Used SCM Imbalance NVME Size NVME Used NVME Imbalance Disabled
----- ----                                 ------- -------- -------- ------------- --------- --------- -------------- --------
tank  8a05bf3a-a088-4a77-bb9f-df989fce7cc8 1-3      3 GB    10 kB    0%            47 GB     0 B       0%             0/32
```

### Destroying a Pool

To destroy a pool labeled `tank`:

```bash
$ dmg pool destroy tank
Pool-destroy command succeeded
```

The pool's UUID can be used instead of the pool label.

To destroy a pool which has active connections (open pool handles will be evicted before pool is
destroyed):

```bash
$ dmg pool destroy tank --force
Pool-destroy command succeeded
```

To destroy a pool despite the existence of associated containers:

```bash
$ dmg pool destroy tank --recursive
Pool-destroy command succeeded
```

Without the --recursive flag, destroy will fail if containers exist in the pool.

### Querying a Pool

The pool query operation retrieves information (i.e., the number of targets,
space usage, rebuild status, property list, and more) about an existing pool.

To query a pool labeled `tank`:

```bash
$ dmg pool query tank
```

The pool's UUID can be used instead of the pool label.
Below is the output for a pool created with SCM space only.

```bash
    pool=47293abe-aa6f-4147-97f6-42a9f796d64a
    Pool 47293abe-aa6f-4147-97f6-42a9f796d64a, ntarget=64, disabled=8
    Pool space info:
    - Target(VOS) count:56
    - SCM:
        Total size: 28GB
        Free: 28GB, min:505MB, max:512MB, mean:512MB
    - NVMe:
        Total size: 0
        Free: 0, min:0, max:0, mean:0
    Rebuild done, 10 objs, 1026 recs
```

The total and free sizes are the sum across all the targets whereas
min/max/mean gives information about individual targets. A min value
close to 0 means that one target is running out of space.

NB: the Versioning Object Store (VOS) may reserve a portion of the
SCM and NVMe allocations to mitigate against fragmentation and for background
operations (e.g., aggregation, garbage collection). The amount of storage
set aside depends on the size of the target and may take up 2+ GB.
Therefore, out of space conditions may occur even while pool query may not
show the minimum free space approaching zero.

The example below shows a rebuild in progress and NVMe space allocated.

```bash
    pool=95886b8b-7eb8-454d-845c-fc0ae0ba5671
    Pool 95886b8b-7eb8-454d-845c-fc0ae0ba5671, ntarget=64, disabled=8
    Pool space info:
    - Target(VOS) count:56
    - SCM:
        Total size: 28GB
        Free: 28GB, min:470MB, max:512MB, mean:509MB
    - NVMe:
        Total size: 56GB
        Free: 28GB, min:470MB, max:512MB, mean:509MB
    Rebuild busy, 75 objs, 9722 recs
```

Additional status and telemetry data is planned to be exported through
management tools and will be documented here once available.

### Upgrading a Pool

The pool upgrade operation upgrades a pool's disk format to the latest
pool format while preserving its data. The existing containers should
be accessible after the upgrade with the same level of functionality as
before the upgrade. New features available at the pool, container or
object level won’t be automatically enabled on existing pools unless
upgrade is performed.

NB: The pool upgrade operation will evict any existing connections
to this pool - pool will be unavailable to connect during pool upgrading.

To see all UpgradeNeeded pools:

```bash
$ dmg pool list
```

Below is an example of pool list

```bash
   Pool  Size   State Used Imbalance Disabled UpgradeNeeded?
   ----  ----   ----- ---- --------- -------- --------------
   pool1 1.0 GB Ready 0%   0%        0/4      1->2
   pool2 1.0 GB Ready 0%   0%        0/4      1->2
```

For the above example, pool1 and pool2 need to be upgraded from pool
version 1 to pool version 2.

NB: jump upgrading is not supported; e.g. upgrading pools created from
DAOS v2.0 to DAOS v2.2 is ok, but not for DAOS v2.0 to v2.4 directly.

The example below shows before and after pool upgrade.

```bash
$ dmg pool get-prop pool1

   Pool pool1 properties:
   Name                                                                             Value
   ----                                                                             -----
   WAL Checkpointing behavior (checkpoint)                                          value not set
   WAL Checkpointing frequency, in seconds (checkpoint_freq)                        not set
   Usage of WAL before checkpoint is triggered, as a percentage (checkpoint_thresh) not set
   EC cell size (ec_cell_sz)                                                        64 KiB
   Performance domain affinity level of EC (ec_pda)                                 1
   Global Version (global_version)                                                  1
   Pool label (label)                                                               pool1
   Pool performance domain (perf_domain)                                            value not set
   Tier placement policy (policy)                                                   type=io_size
   Pool redundancy factor (rd_fac)                                                  0
   Reclaim strategy (reclaim)                                                       lazy
   Performance domain affinity level of RP (rp_pda)                                 3
   Checksum scrubbing mode (scrub)                                                  value not set
   Checksum scrubbing frequency (scrub-freq)                                        not set
   Checksum scrubbing threshold (scrub-thresh)                                      not set
   Self-healing policy (self_heal)                                                  exclude
   Rebuild space ratio (space_rb)                                                   0%
   Pool service replica list (svc_list)                                             [0]
   Pool service redundancy factor (svc_rf)                                          not set
   Upgrade Status (upgrade_status)                                                  not started

$ dmg pool upgrade pool1
   Pool-upgrade command succeeded

$ dmg pool get-prop pool1

   Pool pool1 properties:
   Name                                                                             Value
   ----                                                                             -----
   WAL Checkpointing behavior (checkpoint)                                          timed
   WAL Checkpointing frequency, in seconds (checkpoint_freq)                        5
   Usage of WAL before checkpoint is triggered, as a percentage (checkpoint_thresh) 50
   EC cell size (ec_cell_sz)                                                        64 KiB
   Performance domain affinity level of EC (ec_pda)                                 1
   Global Version (global_version)                                                  1
   Pool label (label)                                                               pool1
   Pool performance domain (perf_domain)                                            root
   Tier placement policy (policy)                                                   type=io_size
   Pool redundancy factor (rd_fac)                                                  0
   Reclaim strategy (reclaim)                                                       lazy
   Performance domain affinity level of RP (rp_pda)                                 3
   Checksum scrubbing mode (scrub)                                                  off
   Checksum scrubbing frequency (scrub-freq)                                        604800
   Checksum scrubbing threshold (scrub-thresh)                                      0
   Self-healing policy (self_heal)                                                  exclude
   Rebuild space ratio (space_rb)                                                   0%
   Pool service replica list (svc_list)                                             [0]
   Pool service redundancy factor (svc_rf)                                          2
   Upgrade Status (upgrade_status)                                                  in progress
```

Duration of upgrade depends on possible format change across different DAOS releases.
It might trigger rebuild to re-place objects data which might be time-consuming.
So `dmg pool upgrade` will return within seconds, but it might take hours to see Pool Status
change from 'in progress' to 'completed'. The pool will stay offline and deny any pool connection until
pool upgrade status becomes 'completed'.

NB: once upgrade status is "completed", then the container/pool can do normal I/O,
but rebuild status might not finish due to reclaim. pool reintegrate/drain/extend can
only proceed once rebuild status is "done".

!!! warning
    Once upgrade was done, upgraded pools will become unavailable if downgrading software.

### Evicting Users

To evict handles/connections to a pool labeled `tank`:

```bash
$ dmg pool evict tank
Pool-evict command succeeded
```

The pool's UUID can be used instead of the pool label.


## Pool Properties

Properties are predefined parameters that the administrator can tune to control
the behavior of a pool.

### Properties Management

Current properties of an existing pool can be retrieved via the `dmg pool
get-prop` command line.

```bash
$ dmg pool get-prop tank
Pool 8a05bf3a-a088-4a77-bb9f-df989fce7cc8 properties:
Name                            Value
----                            -----
EC cell size (ec_cell_sz)       64 kiB
Pool label (label)              tank
Reclaim strategy (reclaim)      lazy
Self-healing policy (self_heal) exclude
Rebuild space ratio (space_rb)  0%
```

All properties can be specified when creating the pool.

```bash
$ dmg pool create --size 50GB --properties reclaim:disabled tank2
Creating DAOS pool with automatic storage allocation: 50 GB NVMe + 6.00% SCM
Pool created with 100.00% SCM/NVMe ratio
-----------------------------------------
  UUID          : 1f265216-5877-4302-ad29-aa0f90df3f86
  Service Ranks : 0
  Storage Ranks : [0-1]
  Total Size    : 50 GB
  SCM           : 50 GB (25 GB / rank)
  NVMe          : 0 B (0 B / rank)

$ dmg pool get-prop tank2
Pool 1f265216-5877-4302-ad29-aa0f90df3f86 properties:
Name                            Value
----                            -----
EC cell size (ec_cell_sz)       64 kiB
Pool label (label)              tank2
Reclaim strategy (reclaim)      disabled
Self-healing policy (self_heal) exclude
Rebuild space ratio (space_rb)  0%
```

Some properties can be modified after pool creation via the `set-prop` option.

```bash
$ dmg pool set-prop tank2 reclaim:lazy
pool set-prop succeeded

$ dmg pool get-prop tank2 reclaim
Pool 1f265216-5877-4302-ad29-aa0f90df3f86 properties:
Name                       Value
----                       -----
Reclaim strategy (reclaim) lazy
```

### Reclaim Strategy (reclaim)

DAOS is a versioned object store that tags every I/O with an epoch number.
This versioning mechanism is the baseline for multi-version concurrency control and
snapshot support. Over time, unused versions need to be reclaimed in order to
release storage space and also simplify the metadata index. This process is
called aggregation.

The reclaim property defines what strategy to use to reclaimed unused version.
Three options are supported:

* "lazy"     : Trigger aggregation only when there is no IO activities or SCM free space is under pressure (default strategy)
* "time"     : Trigger aggregation regularly despite of IO activities.
* "disabled" : Never trigger aggregation. The system will eventually run out of space even if data is being deleted.

### Self-healing Policy (self\_heal)

This property defines whether a failing engine is automatically evicted from the
pool. Once excluded, the self-healing mechanism will be triggered to restore
the pool data redundancy on the surviving storage engines.
Two options are supported: "exclude" (default strategy) and "rebuild".

### Reserved Space for Rebuilds (space\_rb)

This property defines the percentage of total space reserved on each storage
engine for self-healing purpose. The reserved space cannot be consumed by
applications. Valid values are 0% to 100%, the default is 0%.
When setting this property, specifying the percentage symbol is optional:
`space_rb:2%` and `space_rb:2` both specify two percent of storage capacity.

### Default EC Cell Size (ec\_cell\_sz)

This property defines the default erasure code cell size inherited to DAOS
containers. The EC cell size can be between 1kiB and 1GiB,
although it should typically be set to a value between 32kiB and 1MiB.
The default in DAOS 2.0 was 1MiB. The default in DAOS 2.2 is 64kiB.
When setting this property, the cell size can be specified in Bytes
(as a number with no suffix), with a base-10 suffix like `k` or `MB`,
or with a base-2 suffix like `ki` or `MiB`.

### Service Redundancy Factor (svc\_rf)

This property defines the number of faulty replicas the pool service shall try
to tolerate. Valid values are between 0 to 4, inclusive, with 2 being the
default. If specified during a pool create operation, this property overrides
any `--nsvc` options. This property cannot yet be changed afterward.

See [Erasure Code](https://docs.daos.io/v2.6/user/container/#erasure-code) for details on
erasure coding at the container level.

### Properties for Controlling Checkpoints (Metadata on SSD only)

Checkpointing is a background process that flushes VOS metadata from the ephemeral
copy to the metadata blob storing the VOS file, enabling Write Ahead Log (WAL) space
to be reclaimed.  These properties are available to allow a user experiment with
timing of checkpointing.  They are experimental and may be removed in future versions
of DAOS.

#### Checkpoint policy (checkpoint)

This property controls how checkpoints are triggered for each target.  When enabled,
checkpointing will always trigger if there is space pressure in the WAL. There are
three supported options:

* "timed"       : Checkpointing is also triggered periodically (default option).
* "lazy"        : Checkpointing is only triggered when there is WAL space pressure.
* "disabled"    : Checkpointing is disabled.  WAL space may be exhausted.

#### Checkpoint frequency (checkpoint\_freq)

This property controls how often checkpoints are triggered. It is only relevant
if the checkpoint policy is "timed". The value is specified in seconds in the
range [1, 1000000] with a default of 5.  Values outside the range are
automatically adjusted.

#### Checkpoint threshold (checkpoint\_thresh)

This property controls the percentage of WAL usage to automatically trigger a checkpoint.
It is not relevant when the checkpoint policy is "disabled". The value is specified
as a percentage in the range [10-75] with a default of 50. Values outside the range are
automatically adjusted.

#### Reintegration mode (reintegration)

This property controls how reintegration will recover data. Two options are supported:
"data_sync" (default strategy) and "no_data_sync". with "data_sync", reintegration will
discard pool data and trigger rebuild to sync data. While with "no_data_sync", reintegration
only updates pool map to include rank.

NB: with "no_data_sync" enabled, containers will be turned to read-only, daos won't trigger
rebuild to restore the pool data redundancy on the surviving storage engines if there are
dead rank events.

## Access Control Lists

Client user and group access for pools are controlled by
[Access Control Lists (ACLs)](https://docs.daos.io/v2.6/overview/security/#access-control-lists).
Most pool-related tasks are performed using the DMG administrative tool, which
is authenticated by the administrative certificate rather than user-specific
credentials.

Access-controlled client pool accesses include:

* Connecting to the pool.

* Querying the pool.

* Creating containers in the pool.

* Deleting containers in the pool.

This is reflected in the set of supported
[pool permissions](https://docs.daos.io/v2.6/overview/security/#permissions).

A user must be able to connect to the pool in order to access any containers
inside, regardless of their permissions on those containers.

### Ownership

By default, the `dmg pool create` command will use the current user and current
group to set the pool's owner-user and owner-group. This default can be changed
with the `--user` and `--group` options.

Pool ownership conveys no special privileges for access control decisions.
All desired privileges of the owner-user (`OWNER@`) and owner-group (`GROUP@`)
must be explicitly defined by an administrator in the pool ACL.

### ACL at Pool Creation

To create a pool with a custom ACL:

```bash
$ dmg pool create --size <size> --acl-file <path> <pool_label>
```

The ACL file format is detailed in [here](https://docs.daos.io/v2.6/overview/security/#acl-file).

### Displaying ACL

To view a pool's ACL:

```bash
$ dmg pool get-acl --outfile=<path> <pool_label>
```

The output is in the same string format used in the ACL file during creation,
with one Access Control Entry (i.e., ACE) per line.

An example output is presented below:

```bash
$ dmg pool get-acl tank
# Owner: jlombard@
# Owner Group: jlombard@
# Entries:
A::OWNER@:rw
A::bob@:r
A:G:GROUP@:rw
```

### Modifying ACL

For all of these commands using an ACL file, the ACL file must be in the format
noted above for container creation.

#### Overwriting ACL

To replace a pool's ACL with a new ACL:

```bash
$ dmg pool overwrite-acl --acl-file <path> <pool_label>
```

#### Adding and Updating ACEs

To add or update multiple entries in an existing pool ACL:

```bash
$ dmg pool update-acl --acl-file <path> <pool_label>
```

To add or update a single entry in an existing pool ACL:

```bash
$ dmg pool update-acl --entry <ACE> <pool_label>
```

If there is no existing entry for the principal in the ACL, the new entry is
added to the ACL. If there is already an entry for the principal, that entry
is replaced with the new one.

For instance:

```bash
$ dmg pool update-acl -e A::kelsey@:r tank
Pool-update-ACL command succeeded, UUID: 8a05bf3a-a088-4a77-bb9f-df989fce7cc8
# Owner: jlombard@
# Owner Group: jlombard@
# Entries:
A::OWNER@:rw
A::bob@:r
A::kelsey@:r
A:G:GROUP@:rw
```

#### Removing an ACE

To delete an entry for a given principal in an existing pool ACL:

```bash
$ dmg pool delete-acl --principal <principal> <pool_label>
```

The principal corresponds to the principal portion of an ACE that was
set during pool creation or a previous pool ACL operation. For the delete
operation, the principal argument must be formatted as follows:

* Named user: `u:username@`
* Named group: `g:groupname@`
* Special principals: `OWNER@`, `GROUP@`, and `EVERYONE@`

The entry for that principal will be completely removed. This does not always
mean that the principal will have no access. Rather, their access to the pool
will be decided based on the remaining ACL rules.

## Pool Modifications

### Automatic Exclusion

An engine detected as dead by the SWIM monitoring protocol will, by default,
be automatically excluded from all the pools using this engine. The engine
will thus not only be marked as excluded by the system (i.e., in `dmg system
query`), but also reported as disabled in the pool query output (i.e., `dmg
pool query`) for all the impacted pools.

Upon exclusion, the collective rebuild process (i.e., also called self-healing)
will be automatically triggered to restore data redundancy on the
surviving engine.

!!! note
    The rebuild process may consume many resources on each engine and
    is thus throttled to reduce the impact on application performance. This
    current logic relies on CPU cycles on the storage nodes. By default, the
    rebuild process is configured to consume up to 30% of the CPU cycles,
    leaving the other 70% for regular I/O operations.

### Manual Exclusion

An operator can exclude one or more engines or targets from a specific DAOS pool
using the rank the target resides, as well as the target idx on that rank.
If a target idx list is not provided, all targets on the engine rank will be excluded.

To exclude a target from a pool:

```bash
$ dmg pool exclude --rank=${rank} --target-idx=${idx1},${idx2},${idx3} <pool_label>
```

The pool target exclude command accepts 2 parameters:

* The engine rank of the target(s) to be excluded.
* The target indices of the targets to be excluded from that engine rank (optional).

Upon successful manual exclusion, the self-healing mechanism will be triggered
to restore redundancy on the remaining engines.

### Drain

Alternatively, when an operator would like to remove one or more engines or
targets without the system operating in degraded mode, the drain operation can
be used.
A pool drain operation initiates rebuild without excluding the designated engine
or target until after the rebuild is complete.
This allows the drained entity to continue to perform I/O while the rebuild
operation is ongoing. Drain additionally enables non-replicated data to be
rebuilt onto another target whereas in a conventional failure scenario non-replicated
data would not be integrated into a rebuild and would be lost.
Drain operation is not allowed if there are other ongoing rebuild operations, otherwise
it will return -DER_BUSY.

To drain a target from a pool:

```bash
$ dmg pool drain --rank=${rank} --target-idx=${idx1},${idx2},${idx3} $DAOS_POOL
```

The pool target drain command accepts 2 parameters:

* The engine rank of the target(s) to be drained.
* The target indices of the targets to be drained from that engine rank (optional).

### Reintegration

After an engine failure and exclusion, an operator can fix the underlying issue
and reintegrate the affected engines or targets to restore the pool to its
original state.
The operator can either reintegrate specific targets for an engine rank by
supplying a target idx list, or reintegrate an entire engine rank by omitting the list.
Reintegrate operation is not allowed if there are other ongoing rebuild operations,
otherwise it will return -DER_BUSY.

```
$ dmg pool reintegrate $DAOS_POOL --rank=${rank} --target-idx=${idx1},${idx2},${idx3}
```

The pool reintegrate command accepts 3 parameters:

* The label or UUID of the pool that the targets will be reintegrated into.
* The engine rank of the affected targets.
* The target indices of the targets to be reintegrated on that engine rank (optional).

When rebuild is triggered it will list the operations and their related engines/targets
by their engine rank and target index.

```
Target (rank 5 idx 0) is down.
Target (rank 5 idx 1) is down.
...
(rank 5 idx 0) is excluded.
(rank 5 idx 1) is excluded.
```

These should be the same values used when reintegrating the targets.

```
$ dmg pool reintegrate $DAOS_POOL --rank=5 --target-idx=0,1
```

!!! warning
    While dmg pool query and list show how many targets are disabled for each
    pool, there is currently no way to list the targets that have actually
    been disabled. As a result, it is recommended for now to try to reintegrate
    all engine ranks one after the other via `for i in seq $NR_RANKs; do dmg
    pool reintegrate --rank=$i; done`. This limitation will be addressed in the
    next release.

## Pool Extension

### Addition & Space Rebalancing

Full support for online target addition and automatic space rebalancing is
planned for a future release and will be documented here once available.

Until then the following command(s) are placeholders and offer limited
functionality related to Online Server Addition/Rebalancing operations.

An operator can choose to extend a pool to include ranks not currently in the
pool.
This will automatically trigger a server rebalance operation where objects
within the extended pool will be rebalanced across the new storage.
Extend operation is not allowed if there are other ongoing rebuild operations,
otherwise it will return -DER_BUSY.

```
$ dmg pool extend $DAOS_POOL --ranks=${rank1},${rank2}...
```

The pool extend command accepts one required parameter which is a comma
separated list of engine ranks to include in the pool.

The pool rebalance operation will work most efficiently when the pool is
extended to its desired size in a single operation, as opposed to multiple,
small extensions.

### Resize

Support for quiescent pool resize (changing capacity used on each storage node
without adding new ones) is currently not supported and is under consideration.

## Pool Catastrophic Recovery

A DAOS pool is instantiated on each target by a set of pmemobj files
managed by PMDK and SPDK blobs on SSDs. Tools to verify and repair this
persistent data is scheduled for DAOS v3.0 and will be documented here
once available.

Meanwhile, PMDK provides a recovery tool (i.e., pmempool check) to verify
and possibly repair a pmemobj file. As discussed in the previous section, the
rebuild status can be consulted via the pool query and will be expanded
with more information.

## Recovering Container Ownership

Typically users are expected to manage their containers. However, in the event
that a container is orphaned and no users have the privileges to change the
ownership, an administrator can transfer ownership of the container to a new
user and/or group.

To change the owner user:

```bash
$ dmg cont set-owner --pool <UUID> --cont <UUID> --user <owner-user>
```
To change the owner group:

```bash
$ dmg cont set-owner --pool <UUID> --cont <UUID> --group <owner-group>
```

The user and group names are case sensitive and must be formatted as
[DAOS ACL user/group principals](https://docs.daos.io/v2.6/overview/security/#principal).

Because this is an administrative action, it does not require the administrator
to have any privileges assigned in the container ACL.
