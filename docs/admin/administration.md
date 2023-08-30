# DAOS System Administration

## RAS Events

Reliability, Availability, and Serviceability (RAS) related events are
communicated and logged within DAOS and syslog.

### Event Structure

The following table describes the structure of a DAOS RAS event, including
descriptions of mandatory and optional fields.

| Field             | Optional/Mandatory   | Description                                              |
|:----|:----|:----|
| ID                | Mandatory            | Unique event identifier referenced in the manual.        |
| Timestamp (ts)    | Mandatory            | Resolution at the microseconds and include the timezone offset to avoid locality issues.                |
| Hostname (host)   | Optional             | Hostname of the node involved in the event.              |
| Type              | Mandatory            | Event type of STATE\_CHANGE causes an update to the Management Service (MS) database in addition to event being written to SYSLOG. INFO\_ONLY type events are only written to SYSLOG.                                       |
| Severity (sev)    | Mandatory            | Indicates event severity, Error/Warning/Notice.          |
| Msg               | Mandatory            | Human readable message.                                  |
| PID               | Optional             | Identifier of the process involved in the RAS event      |
| TID               | Optional             | Identifier of the thread involved in the RAS event.      |
| Rank              | Optional             | DAOS rank involved in the event.                         |
| Incarnation (inc) | Optional             | Incarnation version of DAOS rank involved in the event. An incarnation of an engine (engine is identified by a rank) is an internal sequence number used to order aliveness events related to an engine.           |
| HWID              | Optional             | Identify hardware components involved in the event. E.g., PCI address for SSD, network interface              |
| JOBID             | Optional             | Identifier of the job involved in the RAS event.         |
| PUUID (pool)      | Optional             | Pool UUID involved in the event, if any.                 |
| CUUID (cont)      | Optional             | Container UUID involved in the event, if relevant.       |
| OID (objid)       | Optional             | Object identifier involved in the event, if relevant.    |
| Control Op (ctlop)| Optional             | Recommended automatic action, if any.                    |
| Data              | Optional             | Specific instance data treated as a blob.                |

Below is an example of a RAS event signaling an exclusion of an unresponsive
engine:

```
&&& RAS EVENT id: [swim_rank_dead] ts: [2021-11-21T13:32:31.747408+0000] host: [wolf-112.wolf.hpdd.intel.com] type: [STATE_CHANGE] sev: [NOTICE] msg: [SWIM marked rank as dead.] pid: [253454] tid: [1] rank: [6] inc: [63a058833280000]
```

### Event List

The following table lists supported DAOS RAS events, including IDs, type,
severity, message, description, and cause.

|Event|Event type|Severity|Message|Description|Cause|
|:----|:----|:----|:----|:----|:----|
| engine\_format\_required|INFO\_ONLY|NOTICE|DAOS engine <idx\> requires a <type\> format|Indicates engine is waiting for allocated storage to be formatted on formatted on instance <idx\> with dmg tool. <type\> can be either SCM or Metadata.|DAOS server attempts to bring-up an engine that has unformatted storage.|
| engine\_died| STATE\_CHANGE| ERROR| DAOS engine <idx\> exited exited unexpectedly: <error\> | Indicates engine instance <idx\> unexpectedly. <error> describes the exit state returned from exited daos\_engine process.| N/A                          |
| engine\_asserted| STATE\_CHANGE| ERROR| TBD| Indicates engine instance <idx> threw a runtime assertion, causing a crash. | An unexpected internal state resulted in assert failure. |
| engine\_clock\_drift| INFO\_ONLY   | ERROR| clock drift detected| Indicates CART comms layer has detected clock skew between engines.| NTP may not be syncing clocks across DAOS system.      |
| pool\_rebuild\_started| INFO\_ONLY| NOTICE   | Pool rebuild started.| Indicates a pool rebuild has started. The event data field contains pool map version and pool operation identifier. | When a pool rank becomes unavailable a rebuild will be triggered.   |
| pool\_rebuild\_finished| INFO\_ONLY| NOTICE| Pool rebuild finished.| Indicates a pool rebuild has finished successfully. The event data field includes the pool map version and pool operation identifier.  | N/A|
| pool\_rebuild\_failed| INFO\_ONLY| ERROR| Pool rebuild failed: <rc\>.| Indicates a pool rebuild has failed. The event data field includes the pool map version and pool operation identifier. <rc\> provides a string representation of DER code.| N/A                          |
| pool\_replicas\_updated| STATE\_CHANGE| NOTICE| List of pool service replica ranks has been updated.| Indicates a pool service replica list has changed. The event contains the new service replica list in a custom payload. | When a pool service replica rank becomes unavailable a new rank is selected to replace it (if available). |
| pool\_durable\_format\_incompat| INFO\_ONLY| ERROR| incompatible layout version: <current\> not in [<min\>, <max\>]| Indicates the given pool's layout version does not match any of the versions supported by the currently running DAOS software.| DAOS engine is started with pool data in local storage that has an incompatible layout version. |
| container\_durable\_format\_incompat| INFO\_ONLY| ERROR| incompatible layout version[: <current\> not in [<min\>, <max\>\]| Indicates the given container's layout version does not match any of the versions supported by the currently running DAOS software.| DAOS engine is started with container data in local storage that has an incompatible layout version.|
| rdb\_durable\_format\_incompatible| INFO\_ONLY| ERROR| incompatible layout version[: <current\> not in [<min\>, <max\>]] OR incompatible DB UUID: <uuid\> | Indicates the given RDB's layout version does not match any of the versions supported by the currently running DAOS software, or the given RDB's UUID does not match the expected UUID (usually because the RDB belongs to a pool created by a pre-2.0 DAOS version).| DAOS engine is started with rdb data in local storage that has an incompatible layout version.|
| swim\_rank\_alive| STATE\_CHANGE| NOTICE| TBD| The SWIM protocol has detected the specified rank is responsive.| A remote DAOS engine has become responsive.|
| swim\_rank\_dead| STATE\_CHANGE| NOTICE| SWIM rank marked as dead.| The SWIM protocol has detected the specified rank is unresponsive.| A remote DAOS engine has become unresponsive.|
| system\_start\_failed| INFO\_ONLY| ERROR| System startup failed, <errors\>| Indicates that a user initiated controlled startup failed. <errors\> shows which ranks failed.| Ranks failed to start.|
| system\_stop\_failed| INFO\_ONLY| ERROR| System shutdown failed during <action\> action, <errors\>  | Indicates that a user initiated controlled shutdown failed. <action\> identifies the failing shutdown action and <errors\> shows which ranks failed.| Ranks failed to stop.|


## System Logging

Engine logging is initially configured by setting the `log_file` and `log_mask`
parameters in the server config file.
The 'DD_MASK' and 'DD_SUBSYS' environment variables can also be defined within the "env\_vars"
list parameter of the engine section of the server config file to tune log output.

Engine log levels can be changed dynamically (at runtime) by setting log masks for a set of
facilities to a given level.
Settings will be applied to all running DAOS I/O Engines present in the configured dmg hostlist
using the command `dmg server set-logmasks [-m <masks>]`.
The command accepts named arguments for masks ('D_LOG_MASK'), streams ('DD_MASK') and subsystems
('DD_SUBSYS).
If no args are passed, then the log masks for each running engine will be reset to the values of
engine "log\_mask" parameter and "env\_vars" 'DD_MASK' and 'DD_SUBSYS' assignments in the server
config file (as set at the time of daos\_server startup).
If a single arg is passed, then this will be used as the log masks setting.

Example usage:
```
dmg server set-logmasks -m ERR,mgmt=DEBUG
```

The masks input string should look like PREFIX1=LEVEL1,PREFIX2=LEVEL2,... where the syntax is
identical to what is expected by the 'D_LOG_MASK' environment variable.
If the 'PREFIX=' part is omitted, then the level applies to all defined facilities (e.g., a value
of 'WARN' sets everything to WARN).

Supported priority levels for engine logging are FATAL, CRIT, ERR, WARN, NOTE, INFO, DEBUG.

For usage of streams ('DD_MASK') and subsystems ('DD_SUBSYS') parameters, logging is described in
detail in the
[`Debugging System`](https://docs.daos.io/v2.6/admin/troubleshooting/#debugging-system)
section.

## System Monitoring

The DAOS servers maintain a set of metrics on I/O and internal state
of the DAOS processes. The metrics collection is very lightweight and
is always enabled. It cannot be manually enabled or disabled.

The DAOS metrics can be accessed locally on each DAOS server,
or remotely by configuring an HTTP endpoint on each server.

### Local metrics collection with daos\_metrics

The `daos-server` package includes the `daos_metrics` command-line tool.
This tool fetches metrics from the local host only.
No configuration is required to use the `daos_metric` command.

By default, `daos_metrics` displays the metrics in a human-readable tree format.
To produce CSV formatted output, use `daos_metrics --csv`.

Each DAOS engine maintains its own metrics.
The `--srv_idx` parameter can be used to specify which engine to query, if there
are multiple engines configured per server.
The default is to query the first engine on the server (index 0).

See `daos_metrics -h` for details on how to filter metrics.

### Configuring the servers for remote metrics collection

Each DAOS server can be configured to provide an HTTP endpoint for metrics
collection. This endpoint presents the data in a format compatible with
[Prometheus](https://prometheus.io).

To enable remote telemetry collection, update the control plane section of
your DAOS server configuration file:

```
telemetry_port: 9191
```

By default, the HTTP endpoint is disabled. The default port number is 9191,
and it is recommended to use this port as it is also the default for the
clients that will collect the metrics.  Each control plane server will present
its local metrics via the endpoint: `http://<host>:<port>/metrics`

### Remote metrics collection with dmg telemetry

The `dmg telemetry` administrative command can be used to query an individual DAOS
server for metrics. Only one DAOS host may be queried at a time.
The command will return information for all engines on that server,
identified by the "rank" attribute.

The metrics have the same names as seen on the telemetry web endpoint.

By default, the `dmg telemetry` command produces human readable output.
The output can be formatted in JSON by running `dmg -j telemetry`.

To list all metrics for the server with their name, type and description:

```
dmg telemetry [-l <host>] [-p <telemetry-port>] metrics list
```

If no host is provided, the default is localhost. The default port is 9191.

To query the values of one or more metrics on the server:

```
dmg telemetry [-l <host>] [-p <telemetry-port>] metrics query [-m <metric_name>]
```

If no host is provided, the default is localhost. The default port is 9191.

Metric names may be provided in a comma-separated list. If no metric names are
provided, all metrics are queried.

### Remote metrics collection with Prometheus

Prometheus is the preferred way to collect metrics from multiple DAOS servers
at the same time.

To integrate with Prometheus, add a new job to your Prometheus server's
configuration file, with the `targets` set to the hosts and telemetry ports of
your DAOS servers:

```yaml
scrape_configs:
- job_name: daos
  scrape_interval: 5s
  static_configs:
  - targets: ['<host>:<telemetry-port>']
```

If there is not already a Prometheus server set up, DMG offers quick setup
options for DAOS.

To install and configure Prometheus on the local machine:

```
dmg telemetry config [-i <install-dir>]
```

DMG will install Prometheus in the directory given with option -i `install-dir`.
Prometheus install path needs to be add to the default system $PATH environment if required.

The Prometheus configuration file will be populated based on the DAOS server
list in your `dmg` configuration file. The Prometheus configuration will be
written to `$HOME/.prometheus.yml`.

To start the Prometheus server with the configuration file generated by `dmg`:

```
prometheus --config-file=$HOME/.prometheus.yml
```

## Storage Operations

Storage subcommands can be used to operate on host storage.
```bash
$ dmg storage --help
Usage:
  dmg [OPTIONS] storage <command>

...

Available commands:
  format    Format SCM and NVMe storage attached to remote servers.
  identify  Blink the status LED on a given VMD device for visual SSD identification.
  query     Query storage commands, including raw NVMe SSD device health stats and internal blobstore health info.
  replace   Replace a storage device that has been hot-removed with a new device.
  scan      Scan SCM and NVMe storage attached to remote servers.
  set       Manually set the device state.
```

Storage query subcommands can be used to get detailed information about how DAOS
is using host storage.
```bash
$ dmg storage query --help
Usage:
  dmg [OPTIONS] storage query <command>

...

Available commands:
  device-health  Query the device health
  list-devices   List storage devices on the server
  list-pools     List pools on the server
  usage          Show SCM & NVMe storage space utilization per storage server
```

### Space Utilization

To query SCM and NVMe storage space usage and show how much space is available to
create new DAOS pools with, run the following command:

- Query Per-Server Space Utilization:
```bash
$ dmg storage query usage --help
Usage:
  dmg [OPTIONS] storage query usage

...
```

The command output shows online DAOS storage utilization, only including storage
statistics for devices that have been formatted by DAOS control-plane and assigned
to a currently running rank of the DAOS system. This represents the storage that
can host DAOS pools.
```bash
$ dmg storage query usage
Hosts   SCM-Total SCM-Free SCM-Used NVMe-Total NVMe-Free NVMe-Used
-----   --------- -------- -------- ---------- --------- ---------
wolf-71 6.4 TB    2.0 TB   68 %     1.5 TB     1.1 TB    27 %
wolf-72 6.4 TB    2.0 TB   68 %     1.5 TB     1.1 TB    27 %
```

Note that the table values are per-host (storage server) and SCM/NVMe capacity
pool component values specified in
[`dmg pool create`](https://docs.daos.io/v2.6/admin/pool_operations/#pool-creationdestroy)
are per rank.
If multiple ranks (I/O processes) have been configured per host in the server
configuration file
[`daos_server.yml`](https://github.com/daos-stack/daos/blob/master/utils/config/daos_server.yml)
then the values supplied to `dmg pool create` should be
a maximum of the SCM/NVMe free space divided by the number of ranks per host.

For example, if 2.0 TB SCM and 10.0 TB NVMe free space is reported by
`dmg storage query usage` and the server configuration file used to start the
system specifies 2 I/O processes (2 "server" sections), the maximum pool size
that can be specified is approximately `dmg pool create -s 1T -n 5T` (may need to
specify slightly below the maximum to take account of negligible metadata
overhead).

### SSD Management

#### Health Monitoring

Useful admin dmg commands to query NVMe SSD health:

- Query Per-Server Metadata:
```bash
$ dmg storage query list-devices --help
Usage:
  dmg [OPTIONS] storage query list-devices [list-devices-OPTIONS]

...

[list-devices command options]
      -r, --rank=         Constrain operation to the specified server rank
      -b, --health        Include device health in results
      -u, --uuid=         Device UUID (all devices if blank)
      -e, --show-evicted  Show only evicted faulty devices
```
```bash
$ dmg storage query list-pools --help
Usage:
  dmg [OPTIONS] storage query list-pools [list-pools-OPTIONS]

...

[list-pools command options]
      -r, --rank=     Constrain operation to the specified server rank
      -u, --uuid=     Pool UUID (all pools if blank)
      -v, --verbose   Show more detail about pools
```
```bash
$ dmg storage scan --nvme-meta --help
Usage:
  dmg [OPTIONS] storage scan [scan-OPTIONS]

...

[scan command options]
      -v, --verbose      List SCM & NVMe device details
      -n, --nvme-health  Display NVMe device health statistics
      -m, --nvme-meta    Display server meta data held on NVMe storage
```

The NVMe storage query list-devices and list-pools commands query the persistently
stored SMD device and pool tables, respectively. The device table maps the internal
device UUID to attached VOS target IDs. The rank number of the server where the device
is located is also listed, along with the current device state. The current device
states are the following:
  - NORMAL: a fully functional device in-use by DAOS
  - EVICTED: the device is no longer in-use by DAOS
  - UNPLUGGED: the device is currently unplugged from the system (may or not be evicted)
  - NEW: the device is plugged and available and not currently in-use by DAOS

To list only devices in the EVICTED state, use the (--show-evicted|-e) option to the
list-devices command.

The transport address is also listed for the device. This is either the PCIe address
for normal NVMe SSDs, or the BDF format address of the backing NVMe SSDs behind a
VMD (Volume Management Device) address. In the example below, the last two listed devices
are both VMD devices with transport addresses in the BDF format behind the VMD address
0000:5d:05.5.

The pool table maps the DAOS pool UUID to attached VOS target IDs and will list all
of the server ranks that the pool is distributed on. With the additional verbose flag,
the mapping of SPDK blob IDs to VOS target IDs will also be displayed.
```bash
$ dmg -l boro-11,boro-13 storage query list-devices
-------
boro-11
-------
  Devices
    UUID:5bd91603-d3c7-4fb7-9a71-76bc25690c19 [TrAddr:0000:8a:00.0]
      Targets:[0 2] Rank:0 State:NORMAL LED:OFF
    UUID:80c9f1be-84b9-4318-a1be-c416c96ca48b [TrAddr:0000:8b:00.0]
      Targets:[1 3] Rank:0 State:NORMAL LED:OFF
    UUID:051b77e4-1524-4662-9f32-f8e4d2542c2d [TrAddr:0000:8c:00.0]
      Targets:[] Rank:0 State:NEW LED:OFF
    UUID:81905b24-be44-4106-8ff9-03002e9dd86a [TrAddr:5d0505:01:00.0]
      Targets:[0 2] Rank:1 State:EVICTED LED:ON
    UUID:2ccb8afb-5d32-454e-86e3-762ec5dca7be [TrAddr:5d0505:03:00.0]
      Targets:[1 3] Rank:1 State:NORMAL LED:OFF
```
```bash
$ dmg -l boro-11,boro-13 storage query list-pools
-------
boro-11
-------
  Pools
    UUID:08d6839b-c71a-4af6-901c-28e141b2b429
      Rank:0 Targets:[0 1 2 3]
      Rank:1 Targets:[0 1 2 3]

$ dmg -l boro-11,boro-13 storage query list-pools --verbose
-------
boro-11
-------
  Pools
    UUID:08d6839b-c71a-4af6-901c-28e141b2b429
      Rank:0 Targets:[0 1 2 3] Blobs:[4294967404 4294967405 4294967407 4294967406]
      Rank:1 Targets:[0 1 2 3] Blobs:[4294967410 4294967411 4294967413 4294967412]

```

- Query Storage Device Health Data:
```bash
$ dmg storage query device-health --help
Usage:
  dmg [OPTIONS] storage query device-health [device-health-OPTIONS]

...

[device-health command options]
      -u, --uuid=     Device UUID
```
```bash
$ dmg storage scan --nvme-health --help
Usage:
  dmg [OPTIONS] storage scan [scan-OPTIONS]

...

[scan command options]
      -v, --verbose      List SCM & NVMe device details
      -n, --nvme-health  Display NVMe device health statistics
      -m, --nvme-meta    Display server meta data held on NVMe storage
```

The NVMe storage query device-health command queries the device health data, including
NVMe SSD health stats and in-memory I/O error and checksum error counters.
The server rank and device state are also listed.
Additionally, vendor-specific SMART stats are displayed, currently for Intel devices only.
Note: A reasonable timed workload > 60 min must be ran for the SMART stats to register
(Raw values are 65535).
Media wear percentage can be calculated by dividing by 1024 to find the percentage of the
maximum rated cycles.
```bash
$ dmg -l boro-11 storage query device-health --uuid=5bd91603-d3c7-4fb7-9a71-76bc25690c19
-------
boro-11
-------
  Devices
    UUID:5bd91603-d3c7-4fb7-9a71-76bc25690c19 [TrAddr:0000:8a:00.0]
      Targets:[0 1 2 3] Rank:0 State:NORMAL
      Health Stats:
        Timestamp:2021-09-13T11:12:34.000+00:00
        Temperature:289K(15C)
        Controller Busy Time:0s
        Power Cycles:0
        Power On Duration:0s
        Unsafe Shutdowns:0
        Media Errors:0
        Read Errors:0
        Write Errors:0
        Unmap Errors:0
        Checksum Errors:0
        Error Log Entries:0
      Critical Warnings:
        Temperature: OK
        Available Spare: OK
        Device Reliability: OK
        Read Only: OK
        Volatile Memory Backup: OK
      Intel Vendor SMART Attributes:
        Program Fail Count:
           Normalized:100%
           Raw:0
        Erase Fail Count:
           Normalized:100%
           Raw:0
        Wear Leveling Count:
           Normalized:100%
           Min:24
           Max:25
           Avg:24
        End-to-End Error Detection Count:0
        CRC Error Count:0
        Timed Workload, Media Wear:65535
        Timed Workload, Host Read/Write Ratio:65535
        Timed Workload, Timer:65535
        Thermal Throttle Status:0%
        Thermal Throttle Event Count:0
        Retry Buffer Overflow Counter:0
        PLL Lock Loss Count:0
        NAND Bytes Written:244081
        Host Bytes Written:52114

```
#### Exclusion and Hotplug

- Manually exclude an NVMe SSD:
```bash
$ dmg storage set nvme-faulty --help
Usage:
  dmg [OPTIONS] storage set nvme-faulty [nvme-faulty-OPTIONS]

...

[nvme-faulty command options]
      -u, --uuid=     Device UUID to set
      -f, --force     Do not require confirmation
```

To manually evict an NVMe SSD (auto eviction will be supported in a future release),
the device state needs to be set faulty by running the following command:
```bash
$ dmg -l boro-11 storage set nvme-faulty --uuid=5bd91603-d3c7-4fb7-9a71-76bc25690c19
NOTICE: This command will permanently mark the device as unusable!
Are you sure you want to continue? (yes/no)
yes
-------
boro-11
-------
  Devices
    UUID:5bd91603-d3c7-4fb7-9a71-76bc25690c19 [TrAddr:]
            Targets:[] Rank:0 State:EVICTED LED:ON
```
The device state will transition from "NORMAL" to "EVICTED" (shown above), during which time the
faulty device reaction will have been triggered (all targets on the SSD will be rebuilt).
The SSD will remain evicted until device replacement occurs.

If an NVMe SSD is faulty, the status LED on the VMD device will be set to an ON state,
represented by a solidly ON amber light.
This LED activity visually indicates a fault and that the device needs to be replaced and is no
longer in use by DAOS.
The LED of the VMD device will remain in this state until replaced by a new device.

!!! note
    Full NVMe hot plug capability will be available and supported in DAOS 2.6 release.
    Use is currently intended for testing only and is not supported for production.

- To use a newly added (hot-inserted) SSD it needs to be unbound from the kernel driver
and bound instead to a user-space driver so that the device can be used with DAOS.

To rebind a SSD on a single host, run the following command (replace SSD PCI address and
hostname with appropriate values):
```bash
$ dmg storage nvme-rebind -a 0000:84:00.0 -l wolf-167
Command completed successfully
```

The device will now be bound to a user-space driver (e.g. VFIO) and can be accessed by
DAOS I/O engine processes (and used in the following `dmg storage replace nvme` command
as a new device).

- Once an engine is using a newly added (hot-inserted) SSD it can be added to the persistent
NVMe config (stored on SCM) so that on engine restart the new device will be used.

To update the engine's persistent NVMe config with the new SSD transport address, run the
following command (replace SSD PCI address, engine index and hostname with appropriate values):
```bash
$ dmg storage nvme-add-device -a 0000:84:00.0 -e 0 -l wolf-167
Command completed successfully
```

The optional [--tier-index|-t] command parameter can be used to specify which storage tier to
insert the SSD into, if specified then the server will attempt to insert the device into the tier
specified by the index, if not specified then the server will attempt to insert the device into
the bdev tier with the lowest index value (the first bdev tier).

The device will now be registered in the engine's persistent NVMe config so that when restarted,
the newly added SSD will be used.

- Replace an excluded SSD with a New Device:
```bash
$ dmg storage replace nvme --help
Usage:
  dmg [OPTIONS] storage replace nvme [nvme-OPTIONS]

...

[nvme command options]
          --old-uuid= Device UUID of hot-removed SSD
          --new-uuid= Device UUID of new device
          --no-reint  Bypass reintegration of device and just bring back online.
```

To replace an NVMe SSD with an evicted device and reintegrate it into use with
DAOS, run the following command:
```bash
$ dmg -l boro-11 storage replace nvme --old-uuid=5bd91603-d3c7-4fb7-9a71-76bc25690c19 --new-uuid=80c9f1be-84b9-4318-a1be-c416c96ca48b
-------
boro-11
-------
  Devices
    UUID:80c9f1be-84b9-4318-a1be-c416c96ca48b [TrAddr:]
      Targets:[] Rank:1 State:NORMAL LED:OFF
```
The old, now replaced device will remain in an "EVICTED" state until it is unplugged.
The new device will transition from a "NEW" state to a "NORMAL" state (shown above).

- Reuse a FAULTY Device:

In order to reuse a device that was previously set as FAULTY and evicted from the DAOS
system, an admin can run the following command (setting the old device UUID to be the
new device UUID):
```bash
$ dmg -l boro-11 storage replace nvme --old-uuid=5bd91603-d3c7-4fb7-9a71-76bc25690c19 --new-uuid=5bd91603-d3c7-4fb7-9a71-76bc25690c19
NOTICE: Attempting to reuse a previously set FAULTY device!
-------
boro-11
-------
  Devices
    UUID:5bd91603-d3c7-4fb7-9a71-76bc25690c19 [TrAddr:]
      Targets:[] Rank:1 State:NORMAL LED:OFF
```
The FAULTY device will transition from an "EVICTED" state back to a "NORMAL" state,
and will again be available for use with DAOS. The use case of this command will mainly
be for testing or for accidental device eviction.

#### Identification

The SSD identification feature is simply a way to quickly and visually locate a
device. It requires the use of Intel VMD (Volume Management Device), which needs
to be physically available on the hardware as well as enabled in the system BIOS.
The feature supports two LED device events: locating a healthy device and locating
an evicted device.

- Locate a Healthy SSD:
```bash
$ dmg storage led identify --help
Usage:
  dmg [OPTIONS] storage led identify [identify-OPTIONS] [ids]

...

[identify command options]
          --reset     Reset blinking LED on specified VMD device back to previous state

[identify command arguments]
  ids:                Comma-separated list of identifiers which could be either VMD backing device
                      (NVMe SSD) PCI addresses or device
```

To identify a single SSD, any of the Device-UUIDs can be used which can be found from
output of the `dmg storage query list-devices` command:
```bash
$ dmg -l boro-11 storage led identify 6fccb374-413b-441a-bfbe-860099ac5e8d
---------
boro-11
---------
  Devices
    TrAddr:850505:0b:00.0 LED:QUICK_BLINK
```

The SSD PCI address can also be used in the command to identify a SSD. The PCI address
should refer to a VMD backing device and can be found from either `dmg storage scan -v`
or `dmg storage query list-devices` commands:
```bash
$ dmg -l boro-11 storage led identify 850505:0b:00.0
---------
boro-11
---------
  Devices
    TrAddr:850505:0b:00.0 LED:QUICK_BLINK
```

To identify multiple SSDs, supply a comma separated list of Device-UUIDs and/or PCI addresses,
adding custom timeout of 5 minutes for LED identification (time to flash LED for):
```bash
$ dmg -l boro-11 storage led identify --timeout 5 850505:0a:00.0,6fccb374-413b-441a-bfbe-860099ac5e8d,850505:11:00.0
---------
boro-11
---------
  Devices
    TrAddr:850505:0a:00.0 LED:QUICK_BLINK
    TrAddr:850505:0b:00.0 LED:QUICK_BLINK
    TrAddr:850505:11:00.0 LED:QUICK_BLINK
```

If a Device-UUID is specified then the command output will display the PCI address of the SSD to
which the Device-UUID belongs and the LED state of that SSD.

Mappings of Device-UUIDs to PCI address can be found in the output of the
`dmg storage query list-devices` command.

An error will be returned if the Device-UUID or PCI address of a non-VMD enabled SSD is specified
in the command.

Upon issuing a device identify command with specified device IDs and optional custom timeout value,
an admin now can quickly identify a device in question.
After issuing the identify command, the status LED on the VMD device is now set to a "QUICK_BLINK"
state, representing a quick, 4Hz blinking amber light.
The device will quickly blink for the specified timeout (in minutes) or the default (2 minutes) if
no value is specified on the command line, after which the LED state will return to the previous
state (faulty "ON" or default "OFF").

- Check LED state of SSDs:

To verify the LED state of SSDs the following command can be used in a similar way to the identify
command:
```bash
$ dmg -l boro-11 storage led check 850505:0a:00.0,6fccb374-413b-441a-bfbe-860099ac5e8d,850505:11:00.0
---------
boro-11
---------
  Devices
    TrAddr:850505:0a:00.0 LED:QUICK_BLINK
    TrAddr:850505:0b:00.0 LED:QUICK_BLINK
    TrAddr:850505:11:00.0 LED:QUICK_BLINK
```

- Locate an Evicted SSD:

If an NVMe SSD is evicted, the status LED on the VMD device is set to a "FAULT"
state, represented by a solidly "ON" amber light. No additional command apart from
the SSD eviction command would be needed, and this would visually indicate that the
device needs to be replaced and is no longer in use by DAOS. The LED of the VMD
device would remain in this state until replaced by a new device.

## System Operations

The DAOS server acting as the access point records details of engines
that join the DAOS system. Once an engine has joined the DAOS system, it is
identified by a unique system "rank". Multiple ranks can reside on the same
host machine, accessible via the same network address.

A DAOS system can be shutdown and restarted to perform maintenance and/or
reboot hosts. Pool data and state will be maintained providing no changes are
made to the rank's metadata stored on persistent memory.

Storage reformat can also be performed after system shutdown. Pools will be
removed and storage wiped.

System commands will be handled by a DAOS Server acting as access point and
listening on the address specified in the DMG config file "hostlist" parameter.
See
[`daos_control.yml`](https://github.com/daos-stack/daos/blob/master/utils/config/daos_control.yml)
for details.

At least one of the addresses in the hostlist parameters should match one of the
"access point" addresses specified in the server config file
[`daos_server.yml`](https://github.com/daos-stack/daos/blob/master/utils/config/daos_server.yml)
that is supplied when starting `daos_server` instances.

- Commands used to manage a DAOS System:
```bash
$ dmg system --help
Usage:
  dmg [OPTIONS] system <command>

...

Available commands:
  cleanup       Clean up all resources associated with the specified machine
  erase         Erase system metadata prior to reformat
  leader-query  Query for current Management Service leader
  list-pools    List all pools in the DAOS system
  query         Query DAOS system status
  start         Perform start of stopped DAOS system
  stop          Perform controlled shutdown of DAOS system
```

### Membership

The system membership refers to the DAOS engine processes that have registered,
or joined, a specific DAOS system.

- Query System Membership:
```bash
$ dmg system query --help
Usage:
  dmg [OPTIONS] system query [query-OPTIONS]

...

[query command options]
      -r, --ranks=      Comma separated ranges or individual system ranks to operate on
          --rank-hosts= Hostlist representing hosts whose managed ranks are to be operated on
      -v, --verbose     Display more member details
```

The `--ranks` takes a pattern describing rank ranges e.g., 0,5-10,20-100.
The `--rank-hosts` takes a pattern describing host ranges e.g. storagehost[0,5-10],10.8.1.[20-100].

The output table will provide system rank mappings to host address and instance
UUID, in addition to the rank state.

DAOS engines run a gossip-based protocol called SWIM that provides efficient
and scalable fault detection. When an engine is reported as unresponsive, a
RAS event is raised and the associated engine is marked as excluded in the
output of `dmg system query`. The engine can be stopped (see next section)
and then restarted to rejoin the system. An failed engine might also be excluded
from the pools it hosted, please check the pool operation section on how to
reintegrate an excluded engine.

### Shutdown

When up and running, the entire system can be shutdown.

- Stop a System:
```bash
$ dmg system stop --help
Usage:
  dmg [OPTIONS] system stop [stop-OPTIONS]

...

[stop command options]
      -r, --ranks=      Comma separated ranges or individual system ranks to operate on
          --rank-hosts= Hostlist representing hosts whose managed ranks are to be operated on
          --force       Force stop DAOS system members
```

The `--ranks` takes a pattern describing rank ranges e.g., 0,5-10,20-100.
The `--rank-hosts` takes a pattern describing host ranges e.g. storagehost[0,5-10],10.8.1.[20-100].

The output table will indicate action and result.

While the engines are stopped, the DAOS servers will continue to
operate and listen on the management network.

!!! warning
    All engines monitor each other and pro-actively exclude unresponsive
    members. It is critical to properly stop a DAOS system as with dmg in
    the case of a planned maintenance on all or a majority of the DAOS
    storage nodes. An abrupt reboot of the storage nodes might result
    in massive exclusion that will take time to recover.

The force option can be passed to for cases when a clean shutown is not working.
Monitoring is not disabled in this case and spurious exclusion might happen,
but the engines are guaranteed to be killed.

dmg also allows to stop a subsection of engines identified by ranks or hostnames.
This is useful to stop (and restart) misbehaving engines.

### Start

The system can be started backup after a controlled shutdown.

- Start a System:
```bash
$ dmg system start --help
Usage:
  dmg [OPTIONS] system start [start-OPTIONS]

...

[start command options]
      -r, --ranks=      Comma separated ranges or individual system ranks to operate on
          --rank-hosts= Hostlist representing hosts whose managed ranks are to be operated on
```

The `--ranks` takes a pattern describing rank ranges e.g., 0,5-10,20-100.
The `--rank-hosts` takes a pattern describing host ranges e.g. storagehost[0,5-10],10.8.1.[20-100].

The output table will indicate action and result.

DAOS I/O Engines will be started.

As for shutdown, a subsection of engines identified by ranks or hostname can be
specified on the command line:

If the ranks were excluded from pools (e.g., unclean shutdown), they will need to
be reintegrated. Please see the pool operation section for more information.

### Storage Reformat

To reformat the system after a controlled shutdown, run the command:

`$ dmg storage format --force`

- `--force` flag indicates that a (re)format operation should be
performed disregarding existing filesystems
- if no record of previously running ranks can be found, reformat is
performed on the hosts that are specified in the `daos_control.yml`
config file's `hostlist` parameter.
- if system membership has records of previously running ranks, storage
allocated to those ranks will be formatted

The output table will indicate action and result.

DAOS I/O Engines will be started, and all DAOS pools will have been removed.

!!! note
    While it should not be required during normal operations, one may still want
    to restart the DAOS installation from scratch without using the DAOS control plane.

    First, ensure all `daos_server` processes on all hosts have been
    stopped, then for each SCM mount specified in the config file
    (`scm_mount` in the `servers` section) umount and wipe FS signatures.

    ```bash
    $ umount /mnt/daos0
    $ umount /mnt/daos1
    $ wipefs -a /dev/pmem0
    $ wipefs -a /dev/pmem0
    ```
    Then restart DAOS Servers and format.


### System Erase

To erase the DAOS sorage configuration, the `dmg system erase`
command can be used. Before doing this, the affected engines need to be
stopped by running `dmg system stop` (if necessary with the `--force` flag).
The erase operation will destroy any pools that may still exist, and will
unconfigure the storage. It will not stop the daos\_server process, so
the `dmg` command can still be used. For example, the system can be
formatted again by running `dmg storage format`.

!!! note
    Note that `dmg system erase` does not currently reset the SCM.
    The `/dev/pmemX` devices will remain mounted,
    and the PMem configuration will not be reset to Memory Mode.
    To completely unconfigure the SCM, it is advisable to run
    `daos_server scm reset` which will completely reset the PMem.
    A reboot will be required to finalize the change of the PMem
    allocation goals.


### System Extension

To add a new server to an existing DAOS system, one should install:

- the relevant certificates
- the server yaml file pointing to the access points of the running
  DAOS system

The daos\_control.yml file should also be updated to include the new DAOS server.

Then starts the daos\_server via systemd and format the new server via
dmg as follows:

```
$ dmg storage format -l ${new_storage_node}
```

new_storage_node should be replaced with the hostname or the IP address of the
new storage node (comma separated list or range of hosts for multiple nodes)
to be added.

Upon completion of the format operation, the new storage nodes will join
the system (this can be checked with `dmg system query -v`).

!!! note
    New pools created after the extension will automatically use the newly added
    nodes (if membership is not restricted on the dmg command line). That being
    said, existing pools won't be automatically extended to use the new servers.
    Please see the pool operation section for how to extend the pool membership.

## Software Upgrade

The DAOS v2.0 wire protocol and persistent layout is not compatible with
previous DAOS versions and would require a reformat and all client and server
nodes to be upgraded to a 2.x version.

!!! warning
    Attempts to start DAOS v2.0 over a system formatted with a previous DAOS
    version will trigger a RAS event and cause all the engines to abort.
    Similarly, a 2.0 DAOS client or engine will refuse to communicate with a
    peer that runs an incompatible version.

DAOS v2.0 will maintain interoperability for both the wire protocol and
persistent layout with any future v2.x versions. That being said, it is
required that all engines in the same system run the same DAOS version.

!!! warning
    Rolling upgrade is not supporting at this time.
