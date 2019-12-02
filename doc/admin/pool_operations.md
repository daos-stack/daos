# DAOS Pool Operations

A DAOS pool is a storage reservation that can span any storage nodes and
is managed by the administrator. The amount of space allocated to a pool
is decided at creation time and can eventually be expanded through the
management interface.

## Pool Creation/Destroy

A DAOS pool can be created and destroyed through the DAOS management API
(see daos_mgmt.h). DAOS also provides a utility called dmg to manage
storage pools from the command line.

**To create a pool:**
```
$ dmg pool create --scm-size=xxG --nvme-size=yyT
```

This command creates a pool distributed across the DAOS servers with a
target size on each server with xxGB of SCM and yyTB of NVMe storage.
The UUID allocated to the newly created pool is printed to stdout
(referred as ${puuid}) as well as the rank where the pool service is
located (referred as ${svcl}).

```
$ dmg pool create --help
...
[create command options]
      -g, --group=     DAOS pool to be owned by given group, format name@domain
      -u, --user=      DAOS pool to be owned by given user, format name@domain
      -a, --acl-file=  Access Control List file path for DAOS pool
      -s, --scm-size=  Size of SCM component of DAOS pool
      -n, --nvme-size= Size of NVMe component of DAOS pool
      -r, --ranks=     Storage server unique identifiers (ranks) for DAOS pool
      -v, --nsvc=      Number of pool service replicas (default: 1)
      -S, --sys=       DAOS system that pool is to be a part of (default: daos_server)
```

The typical output of this command is as follows:

```
$ dmg -i pool create -s 1G -n 10G -g root -u root -S daos
Active connections: [localhost:10001]
Creating DAOS pool with 1GB SCM and 10GB NvMe storage (0.100 ratio)
Pool-create command SUCCEEDED: UUID: 5d6fa7bf-637f-4dba-bcd2-480ad251cdc7,
Service replicas: 0,1
```

This created a pool with UUID 5d6fa7bf-637f-4dba-bcd2-480ad251cdc7,
two pool service replica on rank 0 and 1.

**To destroy a pool:**

```
$ dmg pool destroy --pool=${puuid}
```

## Pool Properties

At creation time, a list of pool properties can be specified through the
API (not supported by the tool yet):

-   DAOS_PROP_CO_LABEL is a string that the administrator can
    associate with a pool. e.g., project A, project B, IO500 test
    pool

-   DAOS_PROP_PO_ACL is the access control list (ACL) associated with
    the pool

-   DAOS_PROP_PO_SPACE_RB is the space to be reserved on each target
    for rebuild purpose.

-   DAOS_PROP_PO_SELF_HEAL defines whether the pool wants
    automatically-trigger, or manually-triggered self-healing.

-   DAOS_PROP_PO_RECLAIM is used to tune the space reclaim strategy
    based on time interval, batched commits or snapshot creation.

While those pool properties are currently stored persistently with pool
metadata, many of them are still under development. Moreover, the
ability to modify some of those properties on an existing pool will also
be provided in a future release.

## Pool Access Control Lists

User and group access for pools is controlled by Access Control Lists (ACLs).
A DAOS ACL is a list of zero or more Access Control Entries (ACEs). ACEs are
the individual rules applied to each access decision.

If no ACL is provided when creating the pool, the default ACL grants read and
write access to the pool's owner-user and owner-group.

### Access Control Entries

ACEs are designated by a colon-separated string format:
`TYPE:FLAGS:IDENTITY:PERMISSIONS`

Available values for these fields:

* TYPE: Allow (A)
* FLAGS: Group (G)
* IDENTITY: See below
* PERMISSIONS: Read (r), Write (w)

#### Identity

The identity (also called the principal) is specified in the name@domain format.
The domain should be left off if the name is a user/group on the local domain.
Currently, this is the only case supported by DAOS.

There are three special identities, `OWNER@`, `GROUP@` and `EVERYONE@`,
which align with User, Group, and Other from traditional POSIX permission bits.
When providing them in the ACE string format, they must be spelled exactly as
written here, in uppercase with no domain appended.

#### Examples

* `A::daos_user@:rw`
  * Allow the UNIX user named daos_user to have read-write access
* `A:G:project_users@:r`
  * Allow anyone in the UNIX group project_users to have read-only access
* `A::EVERYONE@:r`
  * Allow any user not covered by other rules to have read-only access

### Enforcement

Access Control Entries (ACEs) will be enforced in the following order:

* Owner-User
* Named users
* Owner-Group and named groups
* Everyone

In general, enforcement will be based on the first match, ignoring
lower-priority entries. For example, if the user has an ACE for their user
identity, they will not receive the permissions for any of their groups, even if
those group entries have broader permissions than the user entry does. The user
is expected to match at most one user entry.

If no matching user entry is found, but entries match one or more of the user's
groups, enforcement will be based on the union of the permissions of all
matching groups.

By default, if a user matches no ACEs in the list, access will be denied.

### Creating a pool with a custom ACL

To create a pool with a custom ACL:

```
$ dmg pool create --scm-size <size> --acl-file <path>
```

The ACL file is expected to be a text file with one ACE listed on each line. For
example:

```
# Entries:
A::OWNER@:rw
A:G:GROUP@:rw
# Everyone should be allowed to read
A::EVERYONE@:r
```

You may add comments to the ACL file by starting the line with `#`.

### Displaying a pool's ACL

To view a pool's ACL:

```
$ dmg pool get-acl --pool <UUID>
```

The output is in the same string format used in the ACL file during creation,
with one ACE per line.

### Modifying a pool's ACL

For all of these commands using an ACL file, the ACL file must be in the format
noted above for pool creation.

#### Overwriting the ACL

To replace a pool's ACL with a new ACL:

```
$ dmg pool overwrite-acl --pool <UUID> --acl-file <path>
```

#### Updating entries in an existing ACL

To add or update multiple entries in an existing pool ACL:

```
$ dmg pool update-acl --pool <UUID> --acl-file <path>
```

To add or update a single entry in an existing pool ACL:

```
$ dmg pool update-acl --pool <UUID> --entry <ACE>
```

If there is no existing entry for the principal in the ACL, the new entry is
added to the ACL. If there is already an entry for the principal, that entry
is replaced with the new one.

#### Removing an entry from the ACL

To delete an entry for a given principal, or identity, in an existing pool ACL:

```
$ dmg pool delete-acl --pool <UUID> --principal <principal>
```

The principal corresponds to the principal/identity portion of an ACE that was
set during pool creation or a previous pool ACL operation. For the delete
operation, the principal argument must be formatted as follows:

* Named user: `u:username@`
* Named group: `g:groupname@`
* Special principals:
  * `OWNER@`
  * `GROUP@`
  * `EVERYONE@`

The entry for that principal will be completely removed. This does not always
mean that the principal will have no access. Rather, their access to the pool
will be decided based on the remaining ACL rules.

## Pool Query
The pool query operation retrieves information (i.e., the number of targets,
space usage, rebuild status, property list, and more) about a created pool. It
is integrated into the dmg_old utility.

**To query a pool:**

```
$ dmg_old query --svc=${svcl} --pool=${puuid}
```

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

## Pool Modifications

### Target Exclusion and Self-Healing

**To exclude a target from a pool:**

```
$ dmg_old exclude --svc=${svcl} --pool=${puuid} --target=${rank}
```

### Pool Extension

#### Target Addition & Space Rebalancing

Support for online target addition and automatic space rebalancing is
planned for DAOS v1.4 and will be documented here once available.

#### Pool Shard Resize

Support for quiescent pool shard resize is currently not supported and
is under consideration.

## Pool Catastrophic Recovery

A DAOS pool is instantiated on each target by a set of pmemobj files
managed by PMDK and SPDK blobs on SSDs. Tools to verify and repair this
persistent data is scheduled for DAOS v2.4 and will be documented here
once available.

Meanwhile, PMDK provides a recovery tool (i.e., pmempool check) to verify
and possibly repair a pmemobj file. As discussed in the previous section, the
rebuild status can be consulted via the pool query and will be expanded
with more information.
