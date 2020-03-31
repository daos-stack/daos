# DAOS System Administration

## System Monitoring

System monitoring and telemetry data will be provided as part of the
control plane and will be documented in a future revision.

### NVMe SSD Health Monitoring

Useful admin dmg commands to query NVMe SSD health:

- Query Per-Server Metadata (SMD): `dmg storage query smd`

Queries persistently stored device and pool metadata tables. The device table
maps device UUID to attached VOS target IDs. The pool table maps VOS target IDs
to attached SPDK blob IDs.
```bash
$ dmg -l boro-11 storage query smd --devices --pools
boro-11:10001: connected
SMD Device List:
boro-11:10001:
        Device:
                UUID: 5bd91603-d3c7-4fb7-9a71-76bc25690c19
                VOS Target IDs: 0 1 2 3
SMD Pool List:
boro-11:10001:
        Pool:
                UUID: 01b41f76-a783-462f-bbd2-eb27c2f7e326
                VOS Target IDs: 0 1 3 2
                SPDK Blobs: 4294967404 4294967405 4294967407 4294967406
```

- Query Blobstore Health Data: `dmg storage query blobstore-health`

Queries in-memory health data for the SPDK blobstore (i.e, NVMe SSD). This
includes a subset of the SPDK device health stats, as well as I/O error and
checksum counters.
```bash
$ dmg -l boro-11 storage query blobstore-health
  --devuuid=5bd91603-d3c7-4fb7-9a71-76bc25690c19 -l=boro-11:10001
boro-11:10001: connected
Blobstore Health Data:
boro-11:10001:
        Device UUID: 5bd91603-d3c7-4fb7-9a71-76bc25690c19
        Read errors: 0
        Write errors: 0
        Unmap errors: 0
        Checksum errors: 0
        Device Health:
                Error log entries: 0
                Media errors: 0
                Temperature: 289
                Temperature: OK
                Available Spare: OK
                Device Reliability: OK
                Read Only: OK
                Volatile Memory Backup: OK
```

- Query Persistent Device State: `dmg storage query device-state`

Queries the current persistently stored device state of the specified NVMe SSD
(either NORMAL or FAULTY).
```bash
$ dmg storage query device-state --devuuid=5bd91603-d3c7-4fb7-9a71-76bc25690c19
-l=boro-11:10001
boro-11:10001: connected
Device State Info:
boro-11:10001:
        Device UUID: 5bd91603-d3c7-4fb7-9a71-76bc25690c19
        State: NORMAL
```

- Manually Set Device State to FAULTY: `dmg storage set nvme-faulty`

Allows the admin to manually set the device state of the given device to FAULTY,
which will trigger faulty device reaction (all targets on the SSD will be
rebuilt and the SSD will remain in an OUT state until reintegration is
supported).
```bash
$ dmg storage set nvme-faulty --devuuid=5bd91603-d3c7-4fb7-9a71-76bc25690c19
-l=boro-11:10001
boro-11:10001: connected
Device State Info:
boro-11:10001:
        Device UUID: 5bd91603-d3c7-4fb7-9a71-76bc25690c19
        State: FAULTY
```

## System Operations

### Full Shutdown and Restart

A DAOS system can be restarted after a controlled shutdown providing
no configurations changes have been made after the initial format.

The DAOS Control Server instance acting as access point records DAOS
I/O Server instances that join the system in a "membership".

When up and running, the entire system (all I/O Server instances)
can be shut down with the command:
`dmg -l <access_point_addr> system stop`, after which DAOS Control
Servers will continue to operate and listen on the management network.

To start the system again (with no configuration changes) after a
controlled shutdown, run the command
`dmg -l <access_point_addr> system start`, DAOS I/O Servers
managed by DAOS Control Servers will be started.

To query the system membership, run the command
`dmg -l <access_point_addr> system query`, this lists details
(rank/uuid/control address/state) of DAOS I/O Servers in the
system membership.

!!! warning
    Controlled start/stop has some known limitations.
    "start" restarts all configured instances on all harnesses that can
    be located in the system membership, regardless of member state.
    Moreover, supplying the list of ranks to "start" and "stop" is not yet supported

### Fresh Start

To reset the DAOS metadata across all hosts, the system must be reformatted.
First, ensure all `daos_server` processes on all hosts have been
stopped, then for each SCM mount specified in the config file
(`scm_mount` in the `servers` section) umount and wipe FS signatures.

Example illustration with two IO instances specified in the config file:

- `clush -w wolf-[118-121,130-133] umount /mnt/daos1`

- `clush -w wolf-[118-121,130-133] umount /mnt/daos0`

- `clush -w wolf-[118-121,130-133] wipefs -a /dev/pmem1`

- `clush -w wolf-[118-121,130-133] wipefs -a /dev/pmem0`

- Then restart DAOS Servers and format.

### Fault Domain Maintenance and Reintegration

Details on how to drain an individual storage node or fault domain (e.g.
rack) in preparation for maintenance activity and how to reintegrate it
will be provided in a future revision.

### DAOS System Extension

Ability to add new DAOS server instances to a pre-existing DAOS system
will be documented in a future revision.

## Fault Management

DAOS relies on massively distributed single-ported storage. Each target
is thus effectively a single point of failure. DAOS achieves
availability and durability of both data and metadata by providing
redundancy across targets in different fault domains.

### Fault Detection & Isolation

DAOS servers are monitored within a DAOS system through a gossip-based
protocol called SWIM[^1] that provides accurate, efficient, and scalable
server fault detection.

Storage attached to each DAOS target is monitored through periodic local
health assessment. Whenever a local storage I/O error is returned to the
DAOS server, an internal health check procedure will be called
automatically. This procedure makes an overall health assessment by
analyzing the IO error code and device SMART/Health data. If the result
is negative, the target will be marked as faulty, and further I/Os to
this target will be rejected and re-routed.

Once detected, the faulty target or servers (effectively a set of
targets) must be excluded from each pool membership. This process is
triggered either manually by the administrator or automatically (see
the next section for more information). Upon exclusion from the pool map,
each target starts the collective rebuild process automatically to
restore data redundancy. The rebuild process is designed to operate
online while servers continue to process incoming I/O operations from
applications.

Tools to monitor and manage rebuild are still under development.

### Rebuild Throttling

The rebuild process may consume many resources on each server and
can be throttled to reduce the impact on application performance. This
current logic relies on CPU cycles on the storage nodes. By default, the
rebuild process is configured to consume up to 30% of the CPU cycles,
leaving the other 70% for regular I/O operations.

During the rebuild process, the user can set the throttle to guarantee that
the rebuild will not use more resources than the user setting. The user can
only set the CPU cycle for now. For example, if the user set the
throttle to 50, then the rebuild will at most use 50% of the CPU cycle to do
the rebuild job. The default rebuild throttle for CPU cycle is 30. This
parameter can be changed via the daos_mgmt_set_params() API call and
will be eventually available through the management tools.

## Software Upgrade

Interoperability in DAOS is handled via protocol and schema versioning
for persistent data structures. Further instructions on how to manage
DAOS software upgrades will be provided in a future revision.

### Protocol Interoperability

Limited protocol interoperability is provided by the DAOS storage stack.
Version compatibility checks will be performed to verify that:

-   All targets in the same pool run the same protocol version.

-   Client libraries linked with the application may be up to one
    protocol version older than the targets.

If a protocol version mismatch is detected among storage targets in the
same pool, the entire DAOS system will fail to start up and will report
failure to the control API. Similarly, the connection from clients
running a protocol version incompatible with the targets will return an
error.

### Persistent Schema Compatibility and Update

The schema of persistent data structures may evolve from time to time to
fix bugs, add new optimizations, or support new features. To that end,
the persistent data structures support schema versioning.

Upgrading the schema version will not be performed automatically and
must be initiated by the administrator. A dedicated upgrade tool will be
provided to upgrade the schema version to the latest one. All targets in
the same pool must have the same schema version. Version checks are
performed at system initialization time to enforce this constraint.

To limit the validation matrix, each new DAOS release will be published
with a list of supported schema versions. To run with the new DAOS
release, administrators will then need to upgrade the DAOS system to one
of the supported schema versions. New pool shards will always be
formatted with the latest version. This versioning schema only applies
to a data structure stored in persistent memory and not to block storage
that only stores user data with no metadata.

## Storage Scrubbing

Support for end-to-end data integrity is planned for DAOS v1.2 and
background checksum scrubbing for v2.2. Once available, that
functionality will be documented here.

[^1]: https://ieeexplore.ieee.org/stamp/stamp.jsp?arnumber=1028914
