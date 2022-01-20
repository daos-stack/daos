# System Deployment

The DAOS deployment workflow requires to start the DAOS server instances
early on to enable administrators to perform remote operations in parallel
across multiple storage nodes via the dmg management utility. Security is
guaranteed via the use of certificates.
The first type of commands run after installation include network and
storage hardware provisioning and would typically be run from a login
node.

After `daos_server` instances have been started on each storage node for the
first time, `daos_server storage prepare --scm-only` will set PMem storage
into the necessary state for use with DAOS when run on each host.
Then `dmg storage format` formats persistent storage devices (specified in the
server configuration file) on the storage nodes and writes necessary metadata
before starting DAOS I/O engine processes that will operate across the fabric.

To sum up, the typical workflow of a DAOS system deployment consists of the
following steps:

- Configure and start the [DAOS server](#daos-server-setup).

- [Provision Hardware](#hardware-provisioning) on all the storage nodes via the
  dmg utility.

- [Format](#storage-formatting) the DAOS system

- [Set up and start the agent](#agent-startup) on the client nodes

- [Validate](#system-validation) that the DAOS system is operational

Note that starting the DAOS server instances can be performed automatically
on boot if start-up scripts are registered with systemd.

The following subsections will cover each step in more detail.

## DAOS Server Setup

First of all, the DAOS server should be started to allow remote administration
command to be executed via the dmg tool. This section describes the minimal
DAOS server configuration and how to start it on all the storage nodes.

### Example RPM Deployment Workflow

A recommended workflow to get up and running is as follows:

* Install DAOS Server RPMs - `daos_server` systemd services will start in
listening mode which means DAOS I/O engine processes will not be started as the
server config file (default location at `/etc/daos/daos_server.yml`) has not
yet been populated.

* Run `dmg config generate -l <hostset> -a <access_points>` across the entire
hostset (all the storage servers that are now running the `daos_server` service
after RPM install).
The command will only generate a config if hardware setups on all the hosts are
similar and have been given sensible NUMA mappings.
Adjust the hostset until you have a set with homogeneous hardware configurations.

* Once a recommended config file can be generated, copy it to the server config
file default location (`/etc/daos/daos_server.yml`) on each DAOS Server host
and restart all `daos_server` services.
An example command to restart the services is
`clush -w machines-[118-121,130-133] "sudo systemctl restart daos_server"`.
The services should prompt for format on restart and after format is triggered
from `dmg`, the DAOS I/O engine processes should start.

### Server Configuration File

The `daos_server` configuration file is parsed when starting the `daos_server`
process.
The configuration file location can be specified on the command line
(`daos_server -h` for usage) otherwise it will be read from the default location
(`/etc/daos/daos_server.yml`).

Parameter descriptions are specified in
[`daos_server.yml`](https://github.com/daos-stack/daos/blob/master/utils/config/daos_server.yml)
and example configuration files in the
[examples](https://github.com/daos-stack/daos/tree/master/utils/config/examples)
directory.

Any option supplied to `daos_server` as a command line option or flag will
take precedence over equivalent configuration file parameter.

For convenience, active parsed configuration values are written to a temporary
file for reference, and the location will be written to the log.

#### Configuration Options

The example configuration file lists the default empty configuration, listing
all the options (living documentation of the config file). Live examples are
available at
<https://github.com/daos-stack/daos/tree/master/utils/config/examples>

The location of this configuration file is determined by first checking
for the path specified through the -o option of the `daos_server` command
line, if unspecified then `/etc/daos/daos_server.yml` is used.

Refer to the example configuration file (
[`daos_server.yml`](https://github.com/daos-stack/daos/blob/master/utils/config/daos_server.yml)
) for latest information and examples.

At this point of the process, the servers: and provider: section of the yaml
file can be left blank and will be populated in the subsequent sections.

#### Auto generate configuration file

DAOS can attempt to produce a server configuration file that makes optimal use
of hardware on a given set of hosts through the 'dmg config generate' command:

```bash
$ dmg config generate --help
ERROR: dmg: Usage:
  dmg [OPTIONS] config generate [generate-OPTIONS]

Application Options:
...
  -l, --host-list=  comma separated list of addresses <ipv4addr/hostname>
...

[generate command options]
      -a, --access-points=                                 Comma separated list of access point
                                                           addresses <ipv4addr/hostname>
      -e, --num-engines=                                   Set the number of DAOS Engine sections to be
                                                           populated in the config file output. If unset
                                                           then the value will be set to the number of
                                                           NUMA nodes on storage hosts in the DAOS
                                                           system.
      -s, --min-ssds=                                      Minimum number of NVMe SSDs required per DAOS
                                                           Engine (SSDs must reside on the host that is
                                                           managing the engine). Set to 0 to generate a
                                                           config with no NVMe. (default: 1)
      -c, --net-class=[best-available|ethernet|infiniband] Network class preferred (default:
                                                           best-available)
```

The command will output recommended config file if supplied requirements are
met. Requirements will be derived based on the number of NUMA nodes present on
the hosts if '--num-engines' is not specified on the commandline.

- '--num-engines' specifies the number of engine sections to populate in the
config file output.
Each section will specify a persistent memory (PMem) block devices that must be
present on the host in addition to a fabric network interface and SSDs all
bound to the same NUMA node.
If not set explicitly on the commandline, default is the number of NUMA nodes
detected on the host.
- '--min-ssds' specifies the minimum number of NVMe SSDs per-engine that need
to be present on each host.
For each engine entry in the generated config, at least this number of SSDs
must be bound to the NUMA node that matches the affinity of the PMem device
and fabric network interface associated with the engine.
If not set on the commandline, default is "1".
If set to "0" NVMe SSDs will not be added to the generated config and SSD
validation will be disabled.
- '--net-class' specifies preference for network interface class, options are
'ethernet', 'infiband' or 'best-available'.
'best-available' will attempt to choose the most performant (as judged by
libfabric) sets of interfaces and supported provider that match the number and
NUMA affinity of PMem devices.
If not set on the commandline, default is "best-available".

The configuration file that is generated by the command and output to stdout
can be copied to a file and used on the relevant hosts and used as server
config to determine the starting environment for 'daos_server' instances.

Config file output will not be generated in the following cases:
- PMem device count, capacity or NUMA mappings differ on any of the hosts in the
hostlist (the hostlist can be specified either in the 'dmg' config file or on
the commandline).
- NVMe SSD count, PCI address distribution or NUMA affinity differs on any of
the hosts in the host list.
- NUMA node count can't be detected on the hosts or differs on any host in the
host list.
- PMem device count or NUMA affinity doesn't meet the 'num-engines' requirement.
- NVMe device count or NUMA affinity doesn't meet the 'min-ssds' requirement.
- network device count or NUMA affinity doesn't match the configured PMem
devices, taking into account any specified network device class preference
(ethernet or infiniband).

!!! note
    Some CentOS 7.x kernels from before the 7.9 release were known to have a defect
    that prevented `ndctl` from being able to report the NUMA affinity for a
    namespace.
    This prevents generation of dual engine configs using `dmg config generate`
    when running with one of the above-mentioned affected kernels.

#### Certificate Configuration

The DAOS security framework relies on certificates to authenticate
components and administrators in addition to encrypting DAOS control plane
communications. A set of certificates for a given DAOS system may be
generated by running the `gen_certificates.sh` script provided with the DAOS
software if there is not an existing TLS certificate infrastructure. The
`gen_certificates.sh` script uses the `openssl` tool to generate all of the
necessary files. We highly recommend using OpenSSL Version 1.1.1h or higher as
keys and certificates generated with earlier versions are vulnerable to attack.

When DAOS is installed from RPMs, this script is provided in the base `daos` RPM, and
may be invoked in the directory to which the certificates will be written. As part
of the generation process, a new local Certificate Authority is created to handle
certificate signing, and three role certificates are created:

```bash
# /usr/lib64/daos/certgen/gen_certificates.sh
Generating Private CA Root Certificate
Private CA Root Certificate created in ./daosCA
...
Generating Server Certificate
Required Server Certificate Files:
        ./daosCA/certs/daosCA.crt
        ./daosCA/certs/server.key
        ./daosCA/certs/server.crt
...
Generating Agent Certificate
Required Agent Certificate Files:
        ./daosCA/certs/daosCA.crt
        ./daosCA/certs/agent.key
        ./daosCA/certs/agent.crt
...
Generating Admin Certificate
Required Admin Certificate Files:
        ./daosCA/certs/daosCA.crt
        ./daosCA/certs/admin.key
        ./daosCA/certs/admin.crt
```

The files generated under ./daosCA should be protected from unauthorized access and
preserved for future use.

The generated keys and certificates must then be securely distributed to all nodes participating
in the DAOS system (servers, clients, and admin nodes). Permissions for these files should
be set to prevent unauthorized access to the keys and certificates.

Client nodes require:
- CA root cert
- Agent cert
- Agent key

Administrative nodes require:
- CA root cert
- Admin cert
- Admin key

Server nodes require:
- CA root cert
- Server cert
- Server key
- All valid agent certs in the DAOS system (in the client cert directory, see
  config file below)

After the certificates have been securely distributed, the DAOS configuration files must be
updated in order to enable authentication and secure communications. These examples assume
that the configuration and certificate files have been installed under `/etc/daos`:

```yaml
# /etc/daos/daos_server.yml (servers)

transport_config:
  # Location where daos_server will look for Client certificates
  client_cert_dir: /etc/daos/certs/clients
  # Custom CA Root certificate for generated certs
  ca_cert: /etc/daos/certs/daosCA.crt
  # Server certificate for use in TLS handshakes
  cert: /etc/daos/certs/server.crt
  # Key portion of Server Certificate
  key: /etc/daos/certs/server.key
```

```yaml
# /etc/daos/daos_agent.yml (clients)

transport_config:
  # Custom CA Root certificate for generated certs
  ca_cert: /etc/daos/certs/daosCA.crt
  # Agent certificate for use in TLS handshakes
  cert: /etc/daos/certs/agent.crt
  # Key portion of Agent Certificate
  key: /etc/daos/certs/agent.key
```

```yaml
# /etc/daos/daos_control.yml (dmg/admin)

transport_config:
  # Custom CA Root certificate for generated certs
  ca_cert: /etc/daos/certs/daosCA.crt
  # Admin certificate for use in TLS handshakes
  cert: /etc/daos/certs/admin.crt
  # Key portion of Admin Certificate
  key: /etc/daos/certs/admin.key
```

### Server Startup

The DAOS Server is started as a systemd service. The DAOS Server
unit file is installed in the correct location when installing from RPMs.
The DAOS Server will be run as `daos-server` user which will be created
during RPM install.

If you wish to use systemd with a development build, you must copy the service
file from utils/systemd to `/usr/lib/systemd/system`. Once the file is copied,
modify the ExecStart line to point to your `daos_server` binary.

After modifying ExecStart, run the following command:

```bash
$ sudo systemctl daemon-reload
```

Once the service file is installed you can start `daos_server`
with the following commands:

```bash
$ systemctl enable daos_server.service
$ systemctl start daos_server.service
```

To check the component status use:

```bash
$ systemctl status daos_server.service
```

If DAOS Server failed to start, check the logs with:

```bash
$ journalctl --unit daos_server.service
```

After RPM install, `daos_server` service starts automatically running as user
"daos". The server config is read from `/etc/daos/daos_server.yml` and
certificates are read from `/etc/daos/certs`.
With no other admin intervention other than the loading of certificates,
`daos_server` will enter a listening state enabling discovery of storage and
network hardware through the `dmg` tool without any I/O engines specified in the
configuration file. After device discovery and provisioning, an updated
configuration file with a populated per-engine section can be stored in
`/etc/daos/daos_server.yml`, and after reestarting the `daos_server` service
it is then ready for the storage to be formatted.

## DAOS Server Remote Access

Remote tasking of the DAOS system and individual DAOS Server processes can be
performed via the `dmg` utility.

To set the addresses of which DAOS Servers to task, provide either:
- `-l <hostlist>` on the commandline when invoking, or
- `hostlist: <hostlist>` in the control configuration file
[`daos_control.yml`](https://github.com/daos-stack/daos/blob/master/utils/config/daos_control.yml)

Where `<hostlist>` represents a slurm-style hostlist string e.g.
`foo-1[28-63],bar[256-511]`.
The first entry in the hostlist (after alphabetic then numeric sorting) will be
assumed to be the access point as set in the server configuration file.

Local configuration files stored in the user directory will be used in
preference to the default location e.g. `~/.daos_control.yml`.

## Hardware Provisioning

Once the DAOS server started, the storage and network can be configured on the
storage nodes via the dmg utility.

!!! note
    `daos_server` storage commands are not config aware meaning they will not
    read parameters from the server configuration file.

### SCM Preparation

This section addresses how to verify that PMem (Intel(R) Optane(TM) persistent
memory) modules are correctly installed on the storage nodes and how to
configure them in interleaved mode to be used by DAOS.
Instructions for other types of SCM may be covered in the future.

Provisioning the SCM occurs by configuring PMem modules in interleaved memory
regions (interleaved mode) in groups of modules local to a specific socket
(NUMA), and resultant PMem namespaces are defined by a device identifier
(e.g., /dev/pmem0).

PMem preparation is required once per DAOS installation.

This step requires a reboot to enable PMem resource allocation changes to be
read by BIOS.

PMem preparation can be performed with `daos_server storage prepare --scm-only`.

The first time the command is run, the SCM interleaved regions will be created
as resource allocations on any available PMem modules (one region per NUMA
node/socket). The regions are activated after BIOS reads the new resource
allocations.
Upon completion, the storage prepare command will prompt the admin to reboot
the storage node(s) in order for the BIOS to activate the new storage
allocations.
The storage prepare command does not initiate the reboot itself.

After running the command a reboot will be required, the command will then need
to be run for a second time to expose the namespace device to be used by DAOS.

Example usage:

- `clush -w wolf-[118-121,130-133] daos_server storage prepare --scm-only`
after running, the user should be prompted for a reboot.

- `clush -w wolf-[118-121,130-133] reboot`

- `clush -w wolf-[118-121,130-133] daos_server storage prepare --scm-only`
after running, PMem devices (/dev/pmemX namespaces created on the new SCM
regions) should be available on each of the hosts.

On the second run, one namespace per region is created, and each namespace may
take up to a few minutes to create. Details of the pmem devices will be
displayed in JSON format on command completion.

Upon successful creation of the pmem devices, the Intel(R) Optane(TM)
persistent memory is configured and one can move on to the next step.

If required, the pmem devices can be destroyed with the command
`daos_server storage prepare --scm-only --reset`.

All namespaces are disabled and destroyed. The SCM regions are removed by
resetting modules into "MemoryMode" through resource allocations.

Note that undefined behavior may result if the namespaces/pmem kernel
devices are mounted before running reset (as per the printed warning).

A subsequent reboot is required for BIOS to read the new resource
allocations.

### Storage Discovery and Selection

This section covers how to manually detect and select storage devices to be
used by DAOS.
The server configuration file gives an administrator the ability to control
storage selection.

!!! note
    `daos_server` storage commands are not config aware meaning they will not
    read parameters from the server configuration file.

#### Discovery

`dmg storage scan` can be run to query remote running `daos_server`
processes over the management network.

`daos_server storage scan` can be used to query local `daos_server` instances
directly (scans locally-attached SSDs and Intel Persistent Memory Modules usable
by DAOS).
NVMe SSDs need to be made accessible first by running
`daos_server storage prepare --nvme-only`.
The output will be equivalent running `dmg storage scan --verbose` remotely.

```bash
bash-4.2$ dmg storage scan
Hosts        SCM Total             NVMe Total
-----        ---------             ----------
wolf-[71-72] 6.4 TB (2 namespaces) 3.1 TB (3 controllers)

bash-4.2$ dmg storage scan --verbose
------------
wolf-[71-72]
------------
SCM Namespace Socket ID Capacity
------------- --------- --------
pmem0         0         3.2 TB
pmem1         1         3.2 TB

NVMe PCI     Model                FW Revision Socket ID Capacity
--------     -----                ----------- --------- --------
0000:81:00.0 INTEL SSDPED1K750GA  E2010325    1         750 GB
0000:87:00.0 INTEL SSDPEDMD016T4  8DV10171    1         1.6 TB
0000:da:00.0 INTEL SSDPED1K750GA  E2010325    1         750 GB
```

The NVMe PCI field above is what should be used in the server
configuration file to identified NVMe SSDs.

Devices with the same NUMA node/socket should be used in the same per-engine
section of the server configuration file for best performance.

For further info on command usage run `dmg storage --help`.

#### Health

SSD health state can be verified via `dmg storage scan --nvme-health`:

```bash
bash-4.2$ dmg storage scan --nvme-health
-------
wolf-71
-------
PCI:0000:81:00.0 Model:INTEL SSDPED1K750GA  FW:E2010325 Socket:1 Capacity:750 GB
  Health Stats:
    Timestamp:2021-09-13T11:12:34.000+00:00
    Temperature:318K(44.85C)
    Controller Busy Time:0s
    Power Cycles:15
    Power On Duration:10402h0m0s
    Unsafe Shutdowns:13
    Error Count:0
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
       Min:0
       Max:9
       Avg:3
    End-to-End Error Detection Count:0
    CRC Error Count:0
    Timed Workload, Media Wear:65535
    Timed Workload, Host Read/Write Ratio:65535
    Timed Workload, Timer:65535
    Thermal Throttle Status:0%
    Thermal Throttle Event Count:0
    Retry Buffer Overflow Counter:0
    PLL Lock Loss Count:0
    NAND Bytes Written:222897
    Host Bytes Written:71

PCI:0000:da:00.0 Model:INTEL SSDPED1K750GA  FW:E2010325 Socket:1 Capacity:750 GB
  Health Stats:
    Temperature:320K(46.85C)
    Controller Busy Time:0s
    Power Cycles:15
    Power On Duration:10402h0m0s
    Unsafe Shutdowns:13
    Error Count:0
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
       Min:0
       Max:9
       Avg:3
    End-to-End Error Detection Count:0
    CRC Error Count:0
    Timed Workload, Media Wear:65535
    Timed Workload, Host Read/Write Ratio:65535
    Timed Workload, Timer:65535
    Thermal Throttle Status:0%
    Thermal Throttle Event Count:0
    Retry Buffer Overflow Counter:0
    PLL Lock Loss Count:0
    NAND Bytes Written:222897
    Host Bytes Written:71

-------
wolf-72
-------
PCI:0000:81:00.0 Model:INTEL SSDPED1K750GA  FW:E2010435 Socket:1 Capacity:750 GB
  Health Stats:
    Temperature:316K(42.85C)
    Controller Busy Time:8m0s
    Power Cycles:23
    Power On Duration:10399h0m0s
    Unsafe Shutdowns:18
    Error Count:0
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
       Min:0
       Max:9
       Avg:3
    End-to-End Error Detection Count:0
    CRC Error Count:0
    Timed Workload, Media Wear:65535
    Timed Workload, Host Read/Write Ratio:65535
    Timed Workload, Timer:65535
    Thermal Throttle Status:0%
    Thermal Throttle Event Count:0
    Retry Buffer Overflow Counter:0
    PLL Lock Loss Count:0
    NAND Bytes Written:222897
    Host Bytes Written:71

PCI:0000:da:00.0 Model:INTEL SSDPED1K750GA  FW:E2010435 Socket:1 Capacity:750 GB
  Health Stats:
    Temperature:320K(46.85C)
    Controller Busy Time:1m0s
    Power Cycles:23
    Power On Duration:10399h0m0s
    Unsafe Shutdowns:19
    Error Count:0
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
       Min:0
       Max:9
       Avg:3
    End-to-End Error Detection Count:0
    CRC Error Count:0
    Timed Workload, Media Wear:65535
    Timed Workload, Host Read/Write Ratio:65535
    Timed Workload, Timer:65535
    Thermal Throttle Status:0%
    Thermal Throttle Event Count:0
    Retry Buffer Overflow Counter:0
    PLL Lock Loss Count:0
    NAND Bytes Written:222897
    Host Bytes Written:71

```

#### Selection

The next step consists of specifying the devices that should be used by DAOS
in the server configuration YAML file.
The `engines` section of the config is a list with details for each DAOS engine
to be started on the host (one engine will be created for each entry in list).

Devices with the same NUMA rating/node/socket should be colocated on a single
DAOS engine where possible.
See the [server configuration file section](#server-configuration-file) for
more details.

Storage is specified for each engine in a `storage` list with each entry in
the list defining an individual storage tier.

Each tier has a `class` parameter which defines the storage type.
Typical class values are "dcpm" for PMem (Intel(R) Optane(TM) persistent
memory) and "nvme" for NVMe SSDs.

For class == "dcpm", the following parameters should be populated:
- `scm_list` should should contain PMem interleaved-set namespaces
(e.g. `/dev/pmem1`).
Currently the size of the list is limited to 1.
- `scm_mount` gives the desired local directory to be used as the mount point
for DAOS persistent storage mounted on the specified PMem device specified in
`scm_list`.

For class == "nvme", the following parameters should be populated:
- `bdev_list` should be populated with NVMe PCI addresses.

#### Example Configurations

To illustrate, assume a cluster with homogeneous hardware configurations that
returns the following from scan for each host:

```bash
[daos@wolf-72 daos_m]$ dmg -l wolf-7[1-2] -i storage scan --verbose
-------
wolf-7[1-2]
-------
SCM Namespace Socket ID Capacity
------------- --------- --------
pmem0         0         2.90TB
pmem1         1         2.90TB

NVMe PCI     Model                FW Revision Socket ID Capacity
--------     -----                ----------- --------- --------
0000:81:00.0 INTEL SSDPED1K750GA  E2010325    0         750.00GB
0000:87:00.0 INTEL SSDPEDMD016T4  8DV10171    0         1.56TB
0000:da:00.0 INTEL SSDPED1K750GA  E2010325    1         750.00GB
```

In this situation, the configuration file `engines` section could be populated
as follows to establish 2-tier storage:

```yaml
<snip>
port: 10001
access_points: ["wolf-71"] # <----- updated
<snip>
engines:
-
  targets: 16                 # number of I/O service threads per-engine
  first_core: 0               # offset of the first core to bind service threads
  nr_xs_helpers: 0            # count of I/O offload threads
  fabric_iface: eth0          # network interface to use for this engine
  fabric_iface_port: 31416    # network port
  log_mask: ERR               # debug level to start with the engine with
  log_file: /tmp/server1.log  # where to store engine logs
  storage:
  -
    class: dcpm               # type of first storage tier (SCM)
    scm_list: [/dev/pmem0]    # <----- updated
    scm_mount: /mnt/daos0     # where to mount SCM
  -
    class: nvme               # type of second storage tier (NVMe)
    bdev_list: ["0000:87:00.0"] # <----- updated
-
  targets: 16
  first_core: 0
  nr_xs_helpers: 0
  fabric_iface: eth0
  fabric_iface_port: 32416
  log_mask: ERR
  log_file: /tmp/server2.log
  storage:
  -
    class: dcpm               # type of first storage tier (SCM)
    scm_list: [/dev/pmem1]    # <----- updated
    scm_mount: /mnt/daos1     # where to mount SCM
  -
    class: nvme               # type of second storage tier (NVMe)
    bdev_list: ["0000:da:00.0"] # <----- updated
<end>
```

There are a few optional providers that are not built by default. For detailed
information, please refer to the [DAOS build documentation][6].

>**_NOTE_**
>
>DAOS Control Servers will need to be restarted on all hosts after
>updates to the server configuration file.
>
>Pick one host in the system and set `access_points` to list of that
>host's hostname or IP address (don't need to specify port).
>This will be the host which bootstraps the DAOS management service
>(MS).
>
>The support of the optional providers is not guarantee and can be removed
>without further notification.

### Network Configuration

#### Network Scan

The `dmg` utility supports the `network scan` function to display the network
interfaces, related OFI fabric providers and associated NUMA node for each
device.
This information is used to configure the global fabric provider and the unique
local network interface for each I/O engine on the storage nodes.
This section will help you determine what to provide for the `provider`,
`fabric_iface` and `pinned_numa_node` entries in the `daos_server.yml` file.

The following commands are typical examples:
```bash
$ dmg network scan
$ dmg network scan -p all
$ dmg network scan -p ofi+tcp
$ dmg network scan --provider ofi+verbs
```

In the early stages when a `daos_server` has not yet been fully configured and
lacks a declaration of the system's fabric provider, it may be helpful to view
an unfiltered list of scan results.

Use either of these `dmg` commands in the early stages to accomplish
this goal:

```bash
$ dmg network scan
$ dgm network scan -p all
```

Typical network scan results look as follows:
```bash
$ dmg network scan
-------
wolf-29
-------

    -------------
    NUMA Socket 1
    -------------

        Provider    Interfaces
        --------    ----------
        ofi+tcp     ib1

---------
localhost
---------

    -------------
    NUMA Socket 0
    -------------

        Provider    Interfaces
        --------    ----------
        ofi+tcp     ib0, eth0

    -------------
    NUMA Socket 1
    -------------

        Provider    Interfaces
        --------    ----------
        ofi+tcp     ib1
```

Use one of these providers to configure the `provider` in the `daos_server.yml`.
Only one provider may be specified for the entire DAOS installation.
Client nodes must be capable of communicating to the `daos_server` nodes via
the same provider.
Therefore, it is helpful to choose network settings for the `daos_server` that
are compatible with the expected client node configuration.

After the `daos_server.yml` file has been edited and contains a provider,
subsequent `dmg network scan` commands will filter the results based on
that provider.
If it is desired to view an unfiltered list again, issue `dmg network
scan -p all`.

Regardless of the provider in the `daos_server.yml` file, the results may be
filtered to the specified provider with the command `dmg network scan
-p ofi_provider` where `ofi_provider` is one of the available providers from
the list.

The results of the network scan may be used to help configure the I/O engines.

Each I/O engine is configured with a unique `fabric_iface` and optional
`pinned_numa_node`.
The interfaces and NUMA Sockets listed in the scan results map to the
`daos_server.yml` `fabric_iface` and `pinned_numa_node` respectively.
The use of `pinned_numa_node` is optional, but recommended for best
performance.
When specified with the value that matches the network interface, the I/O
engine will bind itself to that NUMA node and to cores purely within that NUMA
node.
This configuration yields the fastest access to that network device.

#### Changing Network Providers

Information about the network configuration is stored as metadata on the DAOS
storage.

If, after initial deployment, the provider must be changed, it is necessary to
reformat the storage devices using `dmg storage format` after the configuration
file has been updated with the new provider.

#### Provider Testing

Then, the `fi_pingpong` test can be used to verify that the targeted OFI
provider works fine:

```bash
node1$ fi_pingpong -p psm2

node2$ fi_pingpong -p psm2 ${IP_ADDRESS_NODE1}

bytes #sent #ack total time  MB/sec  usec/xfer Mxfers/sec
64    10    =10  1.2k  0.00s 21.69   2.95      0.34
256   10    =10  5k    0.00s 116.36  2.20      0.45
1k    10    =10  20k   0.00s 379.26  2.70      0.37
4k    10    =10  80k   0.00s 1077.89 3.80      0.26
64k   10    =10  1.2m  0.00s 2145.20 30.55     0.03
1m    10    =10  20m   0.00s 8867.45 118.25    0.01
```

### CPU Resources

The I/O engine is multi-threaded, and the number of I/O service threads
and helper threads that should be used per engine must be configured
in the `engines:` section of the `daos_server.yml` file.

The number of I/O service threads is configured with the `targets:` setting.
Each storage target manages a fraction of the (interleaved) SCM storage space,
and a fraction of one of the NVMe SSDs that are managed by this engine.
The optimal number of storage targets per engine depends on two conditions:

* For optimal balance regarding the NVMe space, the number of targets should be
an integer multiple of the number of NVMe disks that are configured in the
`bdev_list:` of the engine.
* To obtain the maximum SCM performance, a certain number of targets is needed.
This is device- and workload-dependent, but around 16 targets usually work well.

While not required, it is recommended to also specify a number of
I/O offloading threads with the `nr_xs_helpers:` setting. These threads can
improve performance by offloading activities like checksum calculation and
the dispatching of server-side RPCs from the main I/O service threads.

The server should have sufficiently many physical cores to support the
number of targets plus the additional service threads.


## Storage Formatting

Once the `daos_server` has been restarted with the correct storage devices,
network interface, and CPU threads to use, one can move to the format phase.
When `daos_server` is started for the first time, it enters "maintenance mode"
and waits for a `dmg storage format` call to be issued from the management tool.
This remote call will trigger the formatting of the locally attached storage on
the host for use with DAOS using the parameters defined in the server config file.

`dmg -i -l <host>[,...] storage format` will normally be run on a login
node specifying a hostlist (`-l <host>[,...]`) of storage nodes with SCM/PMem
modules and NVMe SSDs installed and prepared.

Upon successful format, DAOS Control Servers will start DAOS I/O engines that
have been specified in the server config file.

Successful start-up is indicated by the following on stdout:
`DAOS I/O Engine (v0.8.0) process 433456 started on rank 1 with 8 target, 2 helper XS per target, firstcore 0, host wolf-72.wolf.hpdd.intel.com.`

### SCM Format

When the command is run, the pmem kernel devices created on SCM/PMem regions are
formatted and mounted based on the parameters provided in the server config file.

- `scm_mount` specifies the location of the mountpoint to create.
- `scm_class` can be set to `ram` to use a tmpfs in the situation that no SCM/PMem
is available (`scm_size` dictates the size of tmpfs in GB), when set to `dcpm` the device
specified under `scm_list` will be mounted at `scm_mount` path.

### NVMe Format

When the command is run, NVMe SSDs are formatted and set up to be used by DAOS
based on the parameters provided in the server config file.

`bdev_class` can be set to `nvme` to use actual NVMe devices with SPDK for DAOS
storage.
Other `bdev_class` values can be used for emulation of NVMe storage as specified
in the server config file.
`bdev_list` identifies devices to use with a list of PCI addresses (this can be
populated after viewing results from `storage scan` command).

After the format command is run, the path specified by the server configuration
file `scm_mount` parameter should be mounted and should contain a file named
`daos_nvme.conf`.
The file should describe the devices with PCI addresses as listed in the
`bdev_list` parameter of the server config file.
The presence and contents of the file indicate that the specified NVMe SSDs have
been configured correctly for use with DAOS.

The contents of the NVMe SSDs listed in the server configuration file `bdev_list`
parameter will be reset on format.

### Server Format

Before the format command is run, no DAOS metadata should exist under the
path specified by `scm_mount` parameter in the server configuration file.

After the `storage format` command is run, the path specified by the server
configuration file `scm_mount` parameter should be mounted and should contain
the necessary DAOS metadata indicating that the server has been formatted.

When starting, `daos_server` will skip `maintenance mode` and attempt to start
I/O engines if valid DAOS metadata is found in `scm_mount`.

## Agent Setup

This section addresses how to configure the DAOS agents on the storage
nodes before starting it.

### Agent Certificate Generation

The DAOS security framework relies on certificates to authenticate
administrators. The security infrastructure is currently under
development and will be delivered in DAOS v1.0. Initial support for certificates
has been added to DAOS and can be disabled either via the command line or in the
DAOS Agent configuration file. Currently, the easiest way to disable certificate
support is to pass the -i flag to `daos_agent`.

### Agent Configuration File

The `daos_agent` configuration file is parsed when starting the
`daos_agent` process. The configuration file location can be specified
on the command line (`daos_agent -h` for usage) or default location
(`install/etc/daos_agent.yml`). If installed from rpms the default location is
(`/etc/daos/daos_agent.yml`).

Parameter descriptions are specified in
[`daos_agent.yml`](https://github.com/daos-stack/daos/blob/master/utils/config/daos_agent.yml).

Any option supplied to `daos_agent` as a command line option or flag will take
precedence over equivalent configuration file parameter.

For convenience, active parsed config values are written to a temporary file
for reference, and the location will be written to the log.

The following section lists the format, options, defaults, and descriptions
available in the configuration file.

The example configuration file lists the default empty configuration listing
all the options (living documentation of the config file).
Live examples are available
[here](https://github.com/daos-stack/daos/tree/master/utils/config).

The location of this configuration file is determined by first checking
for the path specified through the `-o` option of the `daos_agent` command
line, if not set then `/etc/daos/daos_agent.yml` is used.

Refer to the example configuration file (
[`daos_agent.yml`](https://github.com/daos-stack/daos/blob/master/utils/config/daos_agent.yml)
) for latest information and examples.

#### Defining fabric interfaces manually

By default, the DAOS agent automatically detects all fabric interfaces on the
client node. It selects an appropriate one for DAOS I/O based on the NUMA node
of the client request and the interface type preferences reported by the DAOS
management service.

If the DAOS agent does not detect the fabric interfaces correctly, the
administrator may define them manually in the agent configuration. They must be
organized by NUMA node. If using the verbs provider, the interface domain is
also required.

Example:
```
fabric_ifaces:
-
  numa_node: 0
  devices:
  -
    iface: ib0
    domain: mlx5_0
  -
    iface: ib1
    domain: mlx5_1
-
  numa_node: 1
  devices:
  -
    iface: ib2
    domain: mlx5_2
  -
    iface: ib3
    domain: mlx5_3
```

### Agent Startup

DAOS Agent is a standalone application to be run on each compute node.
It can be configured to use secure communications (default) or can be allowed
to communicate with the control plane over unencrypted channels. The following
example shows `daos_agent` being configured to operate in insecure mode due to
incomplete integration of certificate support as of the 0.6 release and
configured to use a non-default agent configuration file.

To start the DAOS Agent from the command line, run:

```bash
$ daos_agent -i -o <'path to agent configuration file/daos_agent.yml'> &
```

Alternatively, the DAOS Agent can be started as a systemd service. The DAOS Agent
unit file is installed in the correct location when installing from RPMs.
If you want to run the DAOS Agent without certificates (not recommended in production
deployments), you need to add the `-i` option to the systemd `ExecStart` invocation
(see below).

If you wish to use systemd with a development build, you must copy the service
file from `utils/systemd` to `/usr/lib/systemd/system`. Once the file is copied
modify the ExecStart line to point to your in tree `daos_agent` binary.

`ExecStart=/usr/bin/daos_agent -i -o <'path to agent configuration file/daos_agent.yml'>`

Once the service file is installed, you can start `daos_agent`
with the following commands:

```bash
$ sudo systemctl daemon-reload
$ sudo systemctl enable daos_agent.service
$ sudo systemctl start daos_agent.service
```

To check the component status use:

```bash
$ sudo systemctl status daos_agent.service
```

If DAOS Agent failed to start check the logs with:

```bash
$ sudo journalctl --unit daos_agent.service
```

#### Disable Agent Cache (Optional)

In certain circumstances (e.g. for DAOS development or system evaluation), it
may be desirable to disable the DAOS Agent's caching mechanism in order to avoid
stale system information being retained across reformats of a system. The DAOS
Agent normally caches a map of rank->fabric URI lookups as well as client network
configuration data in order to reduce the number of management RPCs required to
start an application. When this information becomes stale, the Agent must be
restarted in order to repopulate the cache with new information. Alternatively,
the caching mechanism may be disabled, with the tradeoff that each application
launch will invoke management RPCs in order to obtain system connection
information.

To disable the DAOS Agent caching mechanism, set the following environment
variable before starting the `daos_agent` process:

`DAOS_AGENT_DISABLE_CACHE=true`

If running from systemd, add the following to the `daos_agent` service file in
the `[Service]` section before reloading systemd and restarting the
`daos_agent` service:

`Environment=DAOS_AGENT_DISABLE_CACHE=true`

[^1]: https://github.com/intel/ipmctl

[^2]: https://github.com/daos-stack/daos/tree/master/utils/config

[^3]: [*https://www.open-mpi.org/faq/?category=running\#mpirun-hostfile*](https://www.open-mpi.org/faq/?category=running#mpirun-hostfile)

[^4]: https://github.com/daos-stack/daos/tree/master/src/control/README.md

[^5]: https://github.com/pmem/ndctl/issues/130

[6]: <../dev/development.md#building-optional-components> (Building DAOS for Development)
