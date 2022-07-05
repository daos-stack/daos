# Pool Operations

A DAOS pool is a storage reservation that can span any storage nodes in a
DAOS system and is managed by the administrator. The amount of space allocated
to a pool is decided at creation time and can eventually be expanded through the
management interface or the `dmg` utility.

## Pool Basics

### Creating a Pool

A DAOS pool can be created and destroyed through the `dmg` utility.

To create a pool labeled `tank`:
```bash
$ dmg pool create --size=<N>TB tank
```

This command creates a pool labeled `tank` distributed across the DAOS servers
with a target size on each server that is comprised of N * 0.94 TB of NVMe storage
and N * 0.06 TB (i.e., 6% of NVMe) of SCM storage. The default SCM:NVMe ratio
may be adjusted at pool creation time as described below.

The UUID allocated to the newly created pool is printed to stdout
as well as the pool service replica ranks.

!!! note
    The --scm-size and --nvme-size options still exist, but should be
    considered deprecated and will likely be removed in a future release.

The label must consist of alphanumeric characters, colon (':'), period ('.'),
hyphen ('-') or underscore ('\_'). The maximum length is set to 127 characters.
Labels that can be parsed as UUID (e.g. 123e4567-e89b-12d3-a456-426614174000)
are forbidden.

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
Creating DAOS pool with automatic storage allocation: 50 GB NVMe + 6.00% SCM
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
with pool service redundancy enabled by default
(pool service replicas on ranks 1-5).

If no redundancy is desired, use --nsvc=1 in order to specify that only
a single pool service replica should be created.

The -t option allows defining the ratio between SCM and NVMe SSD space.
The default value is 6%, which means the space provided after --size
will be distributed as follows:
- 6% is allocated on SCM (i.e., 3GB in the example above)
- 94% is allocated on NVMe SSD (i.e., 47GB in the example above)

Note that it is difficult to determine the usable space by the user, and
currently we cannot provide the precise value. The usable space depends not only
on pool size, but also on number of targets, target size, object class,
storage redundancy factor, etc.

### Listing Pools

To see a list of the pools in your DAOS system:

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
  the difference storage nodes is well balanced. 0% means that there is
  no imbalance and 100% means that out-of-space errors might be returned
  by some storage nodes while space is still available on others.
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

The label can be replaced with the pool UUID.

### Querying a Pool

The pool query operation retrieves information (i.e., the number of targets,
space usage, rebuild status, property list, and more) about a created pool. It
is integrated into the dmg utility.

To query a pool labeled `tank`:

```bash
$ dmg pool query tank
```

The label can be replaced with the pool UUID.
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
Therefore, Out of space conditions may occur even while pool query may not
show min approaching zero.

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

### Evicting Users

To evict handles/connections to a pool labeled `tank`:

```bash
$ dmg pool evict tank
Pool-evict command succeeded
```

The label can be replaced with the pool UUID.

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
EC cell size (ec_cell_sz)       1.0 MiB
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
EC cell size (ec_cell_sz)       1.0 MiB
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

This property defines whether a failing node is automatically evicted from the
pool. Once excluded, the self-healing mechanism will be triggered to restore
the pool data redundancy on the surviving storage nodes.
Two options are supported: "exclude" (default strategy) and "rebuild".

### Reserved Space for Rebuilds (space\_rb)

This property defines the percentage of total space reserved on each storage
node for self-healing purpose. The reserved space cannot be consumed by
applications. Valid values are 0% to 100%, the default is 0%.
When setting this property, specifying the percentage symbol is optional:
`space_rb:2%` and `space_rb:2` both specify two percent of storage capacity.

### Default EC Cell Size (ec\_cell\_sz)

This property defines the default erasure code cell size inherited to DAOS
containers. The EC cell size can be between 1kiB and 1GiB,
although it should typically be set to a value between 32kiB and 1MiB.
The default is 1MiB.
When setting this property, the cell size can be specified in Bytes
(as a number with no suffix), with a base-10 suffix like `k` or `MB`,
or with a base-2 suffix like `ki` or `MiB`.

### Service Redundancy Factor (svc\_rf)

This property defines the number of faulty replicas the pool service shall try
to tolerate. Valid values are between 0 to 4, inclusive, with 2 being the
default. If specified during a pool create operation, this property overrides
any `--nsvc` options. This property cannot yet be changed afterward.

## Access Control Lists

Client user and group access for pools are controlled by
[Access Control Lists (ACLs)](https://docs.daos.io/v2.2/overview/security/#access-control-lists).
Most pool-related tasks are performed using the DMG administrative tool, which
is authenticated by the administrative certificate rather than user-specific
credentials.

Access-controlled client pool accesses include:

* Connecting to the pool.

* Querying the pool.

* Creating containers in the pool.

* Deleting containers in the pool.

This is reflected in the set of supported
[pool permissions](https://docs.daos.io/v2.2/overview/security/#permissions).

A user must be able to connect to the pool in order to access any containers
inside, regardless of their permissions on those containers.

### Ownership

Pool ownership conveys no special privileges for access control decisions. All
desired privileges of the owner-user (`OWNER@`) and owner-group (`GROUP@`) must
be explicitly defined by an administrator in the pool ACL.

### ACL at Pool Creation

To create a pool with a custom ACL:

```bash
$ dmg pool create --size <size> --acl-file <path> <pool_label>
```

The ACL file format is detailed in [here](https://docs.daos.io/v2.2/overview/security/#acl-file).

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
If a target idx list is not provided, all targets on the rank will be excluded.

To exclude a target from a pool:

```bash
$ dmg pool exclude --rank=${rank} --target-idx=${idx1},${idx2},${idx3} <pool_label>
```

The pool target exclude command accepts 2 parameters:

* The engine rank of the target(s) to be excluded.
* The target Indices of the targets to be excluded from that rank (optional).

Upon successful manual exclusion, the self-healing mechanism will be triggered
to restore redundancy on the remaining engines/targets.

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

To drain a target from a pool:

```bash
$ dmg pool drain --rank=${rank} --target-idx=${idx1},${idx2},${idx3} $DAOS_POOL
```

The pool target drain command accepts 2 parameters:

* The engine rank of the target(s) to be drained.
* The target Indices of the targets to be drained from that rank (optional).

### Reintegration

After an engine failure and exclusion, an operator can fix the underlying issue
and reintegrate the affected engines or targets to restore the pool to its
original state.
The operator can either reintegrate specific targets for an engine rank by
supplying a target idx list, or reintegrate an entire rank by omitting the list.

```
$ dmg pool reintegrate $DAOS_POOL --rank=${rank} --target-idx=${idx1},${idx2},${idx3}
```

The pool reintegrate command accepts 3 parameters:

* The label or UUID of the pool that the targets will be reintegrated into.
* The engine rank of the affected targets.
* The target indices of the targets to be reintegrated on that rank (optional).

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

Full Support for online target addition and automatic space rebalancing is
planned for a future release and will be documented here once available.

Until then the following command(s) are placeholders and offer limited
functionality related to Online Server Addition/Rebalancing operations.

An operator can choose to extend a pool to include ranks not currently in the
pool.
This will automatically trigger a server rebalance operation where objects
within the extended pool will be rebalanced across the new storage.

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
persistent data is scheduled for DAOS v2.4 and will be documented here
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
[DAOS ACL user/group principals](https://docs.daos.io/v2.2/overview/security/#principal).

Because this is an administrative action, it does not require the administrator
to have any privileges assigned in the container ACL.
