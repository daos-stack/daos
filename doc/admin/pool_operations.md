DAOS Pool Operations
====================

A DAOS pool is a storage reservation that can span any storage nodes and
is managed by the administrator. The amount of space allocated to a pool
is decided at creation time and can be eventually expanded through the
management interface.

Pool Creation/Destroy
---------------------

A DAOS pool can be created and destroyed through the DAOS management API
(see daos\_mgmt.h). DAOS also provides a utility called dmg to manage
storage pools from the command line.

**To create a pool:**

orterun --ompi-server file:\${urifile} dmg create --size=xxG –nvme=yyT

This command creates a pool distributed across the DAOS servers with a
target size on each server with xxGB of SCM and yyTB of NVMe storage.
The UUID allocated to the newly created pool is printed to stdout
(referred as \${puuid}) as well as the rank where the pool service is
located (referred as \${svcl}).

The typical output of this command is as follows:

orterun --ompi-server file:\${urifile} dmg create --size=xxG –nvme=yyT\
4056fb6d-9fca-4f2d-af8a-cfd57d92a92d 1:2

This created a pool with UUID 4056fb6d-9fca-4f2d-af8a-cfd57d92a92d with
two pool service replica on rank 1 and 2.

**To destroy a pool:**

orterun --ompi-server file:\${urifile} dmg destroy --pool=\${puuid}

Pool Properties
---------------

At creation time, a list of pool properties can be specified through the
API (not supported by the tool yet):

-   DAOS\_PROP\_CO\_LABEL is a string that the administrator can
    associated with a pool. e.g. “project A”, “project B”, “IO500 test
    pool”

-   DAOS\_PROP\_PO\_ACL are access control list (ACL) associated with
    the pool

-   DAOS\_PROP\_PO\_SPACE\_RB is the space to be reserved on each target
    for rebuild purpose.

-   DAOS\_PROP\_PO\_SELF\_HEAL defines whether the pool wants
    automatically-trigger or manually-triggered self-healing.

-   DAOS\_PROP\_PO\_RECLAIM is used to tune the space reclaim strategy
    based on time interval, batched commits or snapshot creation.

While those pool properties are currently stored persistently with pool
metadata, many of them are still under development. Moreover, the
ability to modify some of those properties on an existing pool will also
be eventually provided.

Pool ACLs
---------

Support for per-pool Access Control Lists (ACLs) is under development
and is scheduled for DAOS v1.0. DAOS ACLs will implement a subset of the
NFSv4 ACL standard. This feature will be documented here once available.

The pool query operation retrieves information (i.e. number of targets,
space usage, rebuild status, property list, …) about a created pool. It
is integrated into the dmg utility.

**To query a pool:**

orterun --ompi-server file:\${urifile} dmg query --svc=\${svcl}
--pool=\${puuid}

Below is the output for a pool created with SCM space only.

pool=47293abe-aa6f-4147-97f6-42a9f796d64a

Pool 47293abe-aa6f-4147-97f6-42a9f796d64a, ntarget=64, disabled=8

Pool space info:

- Target(VOS) count:56

- SCM:

Total size: 30064771072

Free: 30044570496, min:530139584, max:536869696, mean:536510187

- NVMe:

Total size: 0

Free: 0, min:0, max:0, mean:0

Rebuild done, 10 objs, 1026 recs

The total and free sizes are the sum across all the targets whereas
min/max/mean gives information about individual targets. A min value
close to 0 means that one target is running out of space.

The example below shows a rebuild in progress and NVMe space allocated.

pool=95886b8b-7eb8-454d-845c-fc0ae0ba5671

Pool 95886b8b-7eb8-454d-845c-fc0ae0ba5671, ntarget=64, disabled=8

Pool space info:

- Target(VOS) count:56

- SCM:

Total size: 30064771072

Free: 29885237632, min:493096384, max:536869696, mean:533664957

- NVMe:

Total size: 60129542144

Free: 29885237632, min:493096384, max:536869696, mean:533664957

Rebuild busy, 75 objs, 9722 recs

Additional status and telemetry data are planned to be exported through
the management API and tool and will be documented here once available.

Pool Modifications
------------------

### Target Exclusion & Self-Healing

**To exclude a target from a pool:**

orterun --ompi-server file:\${urifile} dmg exclude --svc=\${svcl}
--pool=\${puuid} –target=\${rank}

### Pool Extension

#### Target Addition & Space Rebalancing

Support for online target addition and automatic space rebalancing is
planned for DAOS v1.4 and will be documented here once available.

#### Pool Shard Resize

Support for quiescent pool shard resize is currently not supported and
is under consideration.

Pool Catastrophic Recovery
--------------------------

A DAOS pool is instantiated on each target by a set of pmemobj files
managed by PMDK and SPDK blobs on SSDs. Tools to verify and repair this
persistent data is scheduled for DAOS v2.4 and will be documented here
once available.

Meanwhile, PMDK provides a recovery tool (i.e. pmempool check) to verify
and possibly repair a pmemobj file. As show in the previous section, the
rebuild status can be consulted via the pool query and will be expanded
with more information.
