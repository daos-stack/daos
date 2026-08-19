# DAOS Version Interoperability

DAOS supports "N+1" interoperability regarding its wire protocol
and its persistent on-disk data layout.

!!! note
    DAOS version interoperability is supported since DAOS version 2.0.
    DAOS versions before 2.0 were early development versions,
    which are not compatible with later DAOS versions
    and do not provide version interoperability.

DAOS uses a semantic versioning scheme with `MAJOR.MINOR.PATCH`
version numbers for the DAOS software components,
and follows the convention that odd `MINOR` numbers denote
development versions while even `MINOR` numbers denote stable versions.

## Software versions and wire protocol interoperability

To understand the DAOS interoperability requirements regarding DAOS
software versions, and the communication between DAOS software components,
it is important to distinguish the three DAOS node roles:

* DAOS _servers_ contain the physical storage media.
  DAOS servers run the DAOS control plane which is implemented in the `daos_server`
  daemon (usually managed by systemd), and the DAOS data plane which is
  implemented in the `daos_engine` processes that are managed by `daos_server`.

* DAOS _clients_ access the DAOS servers over the network.
  DAOS clients run the `daos_agent` daemon (usually managed by systemd),
  and applications on the DAOS clients access the DAOS storage
  through the `libdaos` library.

* DAOS _admin_ nodes communicate with the DAOS servers through
  the `dmg` management command (or other tools that are using the DAOS
  management API).

A node in a DAOS environment can have one of these node roles,
any combination of two of these node roles,
or all three of these node roles.
The following rules apply for the DAOS software versions:

* On a DAOS _server_, the `daos_server` and `daos_engine`
  versions must be identical.
  For a standard RPM installation, this is always the case because
  both are packaged and installed in the same `daos-server` RPM package.

* On a DAOS _client_, the `daos_agent` version and the `libdaos` library
  version must be identical.
  For a standard RPM installation, this is always the case because
  both are packaged and installed in the same `daos-client` RPM package.

* All DAOS components on a DAOS node must be at the same version.
  For a standard RPM installation, this is ensured through
  RPM dependencies in the DAOS packages.

* All DAOS _servers_ that are members of the same DAOS _system_
  must be at the same DAOS version.

* Not all _client_ nodes need to have identical DAOS versions:
  It is recommended that all nodes in a DAOS client cluster
  that run parallel applications have the same DAOS version.
  But different client clusters can run different DAOS versions.

* DAOS _admin_ nodes must have the same `MAJOR.MINOR` version
  as the DAOS servers they are managing,
  but they may differ in their `PATCH` level.

!!! warning
    TODO: Do we really support `daos-admin`
    at a lower `PATCH` level than the servers??

* The `MAJOR.MINOR` version of DAOS _client_ nodes can differ from
  the `MAJOR.MINOR` version of the DAOS _servers_ by one minor release.
  For example, clients at version 2.6 can communicate with servers
  at versions 2.4, 2.6 and 2.8.
  Similarly, servers at version 2.6 can communicate with clients
  at version 2.4, 2.6 and 2.8.

!!! warning
    On DAOS clients at an older `MAJOR.MINOR` version than the
    `MAJOR.MINOR` version of the DAOS servers, pool connects
    may be rejected if the pool has a newer pool layout version
    that what the client supports. See below for details.

The following table visually depicts the above interoperability
policies for all major components in a DAOS environment:

|            |daos\_server|daos\_engine|daos\_agent|libdaos|admin (dmg)|
|:---        |:---:       |:---:       |:---:      |:---:  |:---:      |
|daos\_server|   x.y.z    |   x.y.z    |  x.(y±1)  |  n/a  |    x.y    |
|daos\_engine|   x.y.z    |   x.y.z    |    n/a    |x.(y±1)|    n/a    |
|daos\_agent |  x.(y±1)   |    n/a     |    n/a    | x.y.z |    n/a    |
|libdaos     |    n/a     |  x.(y±1)   |   x.y.z   |  n/a  |    n/a    |
|admin (dmg) |   x.y      |    n/a     |    n/a    |  n/a  |    n/a    |

Key:

* x.y.z: Major.Minor.Patch must be equal.

* x.y: Major.Minor must be equal.

* x.(y±1): Major must be equal. Minor must be equal or -1/+1 release version.

* n/a: Components do not communicate.

## On-disk pool layout versions

Over time, new DAOS releases have introduced new functionality
that has resulted in changes to the on-disk data layout.
DAOS pools have a `global_version` pool property that enumerates
these pool layout versions. The following pool layout versions exist:

* `global_version=2` was introduced in DAOS 2.4.

* `global_version=3` was introduced in DAOS 2.6.

* `global_version=4` was introduced in DAOS 2.8.

When creating a new DAOS pool with the `dmg pool create` command,
the pool will be created with the highest pool layout version
that is supported by the software version running on the DAOS servers.

When the DAOS software version is upgraded on the DAOS servers,
DAOS pools that have been created with an older DAOS software level
will retain their old pool layout version.
No automatic changes to the on-disk data layout
are performed as part of a DAOS software upgrade.

For example, a pool that was created at DAOS version 2.6 will remain
at the pool layout version 3 when the DAOS servers are upgraded to
DAOS version 2.8.
In such a scenario, DAOS version 2.6 clients will still be able
to connect to such pools at layout version 3 (served by DAOS servers
that are running DAOS software version 2.8).
On the other hand, new pools created on DAOS version 2.8 servers
will have pool layout version 4, which is not supported by DAOS 2.6,
so pool connects from DAOS 2.6 clients to those newer pools will fail.

The `dmg pool upgrade` command is available to upgrade a pool
to the latest pool layout version after a software upgrade.
In line with the "N+1" software version interoperability,
the `dmg pool upgrade` command supports pool layout upgrades
from the previous (second highest) layout version
to the current (highest) layout version.

For example, on a DAOS version 2.8 system
a pool with layout version 3 can be upgraded to layout version 4.
But a pool with layout version 2 cannot be directly upgraded to
layout version 4 on a DAOS version 2.8 system:
It has to be upgraded to layout version 3 first
(while the DAOS system is running DAOS software version 2.6).

!!! warning
    After a pool has been upgraded to the latest pool layout version,
    DAOS clients that are still running the previous DAOS software
    version will no longer be able to access that pool.
    Plan the sequence of software accordingly, ideally upgrading
    all client clusters before upgrading the DAOS servers.

## Pool upgrade examples

Example of listing the `global_version` pool property of a pool:

```
dmg pool get-prop pool01 global_version
Pool pool01 properties:
Name                            Value
----                            -----
Global Version (global_version) 4

dmg pool get-prop -j pool01 global_version
{
  "response": [
    {
      "name": "global_version",
      "description": "Global Version",
      "value": 4
    }
  ],
  "error": null,
  "status": 0
}
```

When listing all DAOS pools with the verbose `dmg pool list -v` command,
an `UpgradeNeeded?` status will be shown. This will identify any pools
for which the current pool layout version is smaller than the latest
pool layout version that supported by this DAOS software version:

```
dmg pool list -v
Label  UUID                                 State SvcReps Meta Size Meta Used Meta Imbalance Data Size Data Used Data Imbalance Disabled UpgradeNeeded? Rebuild State
-----           ----                                 ----- ------- --------- --------- -------------- --------- --------- -------------- -------- -------------- -------------
pool01 deeeed95-9e49-4391-839b-5a46353f3268 Ready [0-1]   399 GB    20 GB     0%             57 TB     1.3 GB    0%             0/32     None           idle
```

In the output above, pool01 is already at the latest layout version
so the `UpgradeNeeded?` column shows `None`.

For an individual pool, the `dmg pool query -j` command will report
the current pool layout version as `pool_layout_ver` and
the latest supported pool layout version as `upgrade_layout_ver`:

```
dmg pool query pool01 -j | grep layout_ver
    "pool_layout_ver": 4,
    "upgrade_layout_ver": 4,
```

The `dmg pool list -j` command with JSON output will report the same
two fields for each of the pools in the system:

```
dmg pool list -j
{
  "response": {
    "status": 0,
    "pools": [
      {
        "query_mask": "disabled_engines,rebuild,space",
        "state": "Ready",
        "uuid": "5e3beca4-c418-4b07-8844-9bc2eee62ac5",
        "label": "pool01",
        ...
        "pool_layout_ver": 4,
        "upgrade_layout_ver": 4,
        ...
      }
    ]
  },
  "error": null,
  "status": 0
}
```

To perform an upgrade of a pool's on-disk data layout,
run the `dmg pool upgrade` command. This is an asynchronous
operation, and the command should immediately return with a
"Pool-upgrade command succeeded" message.  To check progress,
the `upgrade_status` pool property can be queried:

```
dmg pool get-prop pool01 upgrade_status
Pool pool01 properties:
Name                            Value
----                            -----
Upgrade Status (upgrade_status) not started

dmg pool get-prop -j pool01 upgrade_status
{
  "response": [
    {
      "name": "upgrade_status",
      "description": "Upgrade Status",
      "value": "not started"
    }
  ],
  "error": null,
  "status": 0
}
```

The `dmg pool upgrade` command has to be run for each pool individually,
there is no option to upgrade all pools with a single command invocation.
Invoking a pool upgrade for a pool that is already at the latest layout
version is a no-op: Like most `dmg` commands, the operation is idempotent.
