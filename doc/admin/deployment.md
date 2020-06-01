# System Deployment

The DAOS deployment workflow requires to start the DAOS server instances
early on to enable administrators to perform remote operations in parallel
across multiple storage nodes via the dmg management utility. Security is
guaranteed via the use of certificates.
The first type of commands run after installation include network and
storage hardware provisioning and would typically be run from a login
node.

After `daos_server` instances have been started on each storage node
for the first time, `dmg storage prepare` will set DCPM storage into the
necessary state for use with DAOS.
Then `dmg storage format` formats persistent storage devices
(specified in the server configuration file) on the storage nodes
and writes necessary metadata before starting DAOS I/O processes that
will operate across the fabric.

To sum up, the typical workflow of a DAOS system deployment consists of the
following steps:

- Configure and start the [DAOS server](#daos-server-setup).

- [Provision Hardware](#hardware-provisioning) on all the storage nodes via the
  dmg utility.

- [Format](#storage-formatting) the DAOS system

- [Set up and start the agent](#agent-configuration-and-startup) on the client nodes

- [Validate](#system-validation) that the DAOS system is operational

Note that starting the DAOS server instances can be performed automatically
on boot if start-up scripts are registered with systemd.

The following subsections will cover each step in more detail.

Before getting started, please make sure to review and complete the
[pre-flight checklist](#preflight-checklist) below.

## Preflight Checklist

This section covers the preliminary setup required on the compute and
storage nodes before deploying DAOS.

### Enable IOMMU (Optional)

In order to run the DAOS server as a non-root user with NVMe devices, the hardware
must support virtualized device access, and it must be enabled in the system BIOS.
On Intel® systems, this capability is named Intel® Virtualization Technology for
Directed I/O (VT-d). Once enabled in BIOS, IOMMU support must also be enabled in
the Linux kernel. Exact details depend on the distribution, but the following
example should be illustrative:

```bash
# Enable IOMMU on CentOS 7
# All commands must be run as root/sudo!

$ sudo vi /etc/default/grub # add the following line:
GRUB_CMDLINE_LINUX_DEFAULT="intel_iommu=on"

# after saving the file, run the following to reconfigure
# the bootloader:
$ sudo grub2-mkconfig --output=/boot/grub2/grub.cfg

# if the command completed with no errors, reboot the system
# in order to make the changes take effect
$ sudo reboot
```

!!! warning
    VFIO support is a new feature for DAOS 1.2 and has been tested on the
    following platforms:
    •	CentOS 7.7

### Time Synchronization

The DAOS transaction model relies on timestamps and requires time to be
synchronized across all the storage and client nodes. This can be done
using NTP or any other equivalent protocol.

### Runtime Directory Setup

DAOS uses a series of Unix Domain Sockets to communicate between its
various components. On modern Linux systems, Unix Domain Sockets are
typically stored under `/run` or `/var/run` (usually a symlink to `/run`)
and are a mounted tmpfs file system. There are several methods of
ensuring the necessary directories are setup.

A sign that this step may have been missed is when starting `daos_server`
or `daos_agent`, you may see the message:
```bash
$ mkdir /var/run/daos_server: permission denied
Unable to create socket directory: /var/run/daos_server
```
#### Non-default Directory

By default, `daos_server` and `daos_agent` will use the directories
`/var/run/daos_server` and `/var/run/daos_agent` respectively. To change
the default location that `daos_server` uses for its runtime directory,
either uncomment and set the `socket_dir` configuration value in
`install/etc/daos_server.yml`, or pass the location to `daos_server` on
the command line using the -d flag. For the `daos_agent`, an alternate
location can be passed on the command line using the `--runtime_dir` flag.

#### Default Directory (non-persistent)

Files and directories created in `/run` and `/var/run` only survive until
the next reboot. However, if reboots are infrequent, an easy solution
while still utilizing the default locations is to create the
required directories manually. To do this execute the following commands.

`daos_server`:
```bash
$ mkdir /var/run/daos_server
$ chmod 0755 /var/run/daos_server
$ chown user:user /var/run/daos_server (where user is the user you
    will run daos_server as)
```
`daos_agent`:
```bash
$ mkdir /var/run/daos_agent
$ chmod 0755 /var/run/daos_agent
$ chown user:user /var/run/daos_agent (where user is the user you
    will run daos_agent as)
```

#### Default Directory (persistent)

If the server hosting `daos_server` or `daos_agent` will be rebooted often,
systemd provides a persistent mechanism for creating the required
directories called `tmpfiles.d`. This mechanism will be required every
time the system is provisioned and requires a reboot to take effect.

To tell systemd to create the necessary directories for DAOS:

-   Copy the file `utils/systemd/daosfiles.conf` to `/etc/tmpfiles.d`

-   Modify the copied file to change the user and group fields
    (currently `daos`) to the user daos will be run as

-   Reboot the system, and the directories will be created automatically
    on all subsequent reboots.

### Elevated Privileges

DAOS employs a privileged helper binary (`daos_admin`) to perform tasks
that require elevated privileges on behalf of `daos_server`.

#### Privileged Helper Configuration

When DAOS is installed from RPM, the `daos_admin` helper is automatically installed
to the correct location with the correct permissions. The RPM creates a "daos_admins"
system group and configures permissions such that `daos_admin` may only be invoked
from `daos_server`.

For non-RPM installations, there are two supported scenarios:

1. `daos_server` is run as root, which means that `daos_admin` is also invoked as root,
and therefore no additional setup is necessary
2. `daos_server` is run as a non-root user, which means that `daos_admin` must be
manually installed and configured

The steps to enable the second scenario are as follows (steps are assumed to be
running out of a DAOS source tree which may be on a NFS share):

```bash
$ chmod -x $SL_PREFIX/bin/daos_admin # prevent this copy from being executed
$ sudo cp $SL_PREFIX/bin/daos_admin /usr/bin/daos_admin
$ sudo chmod 4755 /usr/bin/daos_admin # make this copy setuid root
$ sudo mkdir -p /usr/share/daos/control # create symlinks to SPDK scripts
$ sudo ln -sf $SL_PREFIX/share/daos/control/setup_spdk.sh \
           /usr/share/daos/control
$ sudo mkdir -p /usr/share/spdk/scripts
$ sudo ln -sf $SL_PREFIX/share/spdk/scripts/setup.sh \
           /usr/share/spdk/scripts
$ sudo ln -sf $SL_PREFIX/share/spdk/scripts/common.sh \
           /usr/share/spdk/scripts
$ sudo ln -s $SL_PREFIX/include \
           /usr/share/spdk/include
```

!!! note
    The RPM installation is preferred for production scenarios. Manual
    installation is most appropriate for development and predeployment
    proof-of-concept scenarios.

## DAOS Server Setup

First of all, the DAOS server should be started to allow remote administration
command to be executed via the dmg tool. This section describes the minimal
DAOS server configuration and how to start it on all the storage nodes.

### Server Configuration File

The `daos_server` configuration file is parsed when starting the
`daos_server` process. The configuration file location can be specified
on the command line (`daos_server -h` for usage) or default location
(`install/etc/daos_server.yml`).

Parameter descriptions are specified in [`daos_server.yml`](https://github.com/daos-stack/daos/blob/master/utils/config/daos_server.yml)
and example configuration files in the [examples](https://github.com/daos-stack/daos/tree/master/utils/config/examples)
directory.

Any option supplied to `daos_server` as a command line option or flag will
take precedence over equivalent configuration file parameter.

For convenience, active parsed configuration values are written to a temporary
file for reference, and the location will be written to the log.

#### Configuration Options

The example configuration file lists the default empty configuration, listing all the
options (living documentation of the config file). Live examples are
available at
<https://github.com/daos-stack/daos/tree/master/utils/config/examples>

The location of this configuration file is determined by first checking
for the path specified through the -o option of the `daos_server` command
line. Otherwise, /etc/daos_server.conf is used.

Refer to the example configuration file ([daos_server.yml](https://github.com/daos-stack/daos/blob/master/utils/config/daos_server.yml))
for latest information and examples.

At this point of the process, the servers: and provider: section of the yaml
file can be left blank and will be populated in the subsequent sections.

#### Certificate Configuration

The DAOS security framework relies on certificates to authenticate
components and administrators in addition to encrypting DAOS control plane
communications. A set of certificates for a given DAOS system may be
generated by running the `gen_certificates.sh` script provided with the DAOS
software if there is not an existing TLS certificate infrastructure.

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
  client_cert_dir: /etc/daos/clients
  # Custom CA Root certificate for generated certs
  ca_cert: /etc/daos/daosCA.crt
  # Server certificate for use in TLS handshakes
  cert: /etc/daos/server.crt
  # Key portion of Server Certificate
  key: /etc/daos/server.key
```

```yaml
# /etc/daos/daos_agent.yml (clients)

transport_config:
  # Custom CA Root certificate for generated certs
  ca_cert: /etc/daos/daosCA.crt
  # Agent certificate for use in TLS handshakes
  cert: /etc/daos/agent.crt
  # Key portion of Agent Certificate
  key: /etc/daos/agent.key
```

```yaml
# /etc/daos/daos_control.yml (dmg/admin)

transport_config:
  # Custom CA Root certificate for generated certs
  ca_cert: /etc/daos/daosCA.crt
  # Admin certificate for use in TLS handshakes
  cert: /etc/daos/admin.crt
  # Key portion of Admin Certificate
  key: /etc/daos/admin.key
```

### Server Startup

One instance of the `daos_server` process is to be started per
storage node. The server can be started either individually (e.g. independently
on each storage node via systemd) or collectively (e.g. pdsh, mpirun or as a
Kubernetes Pod).

#### Parallel Launcher

Practically any parallel launcher can be used to start the DAOS server
collectively on a set of storage nodes. pdsh, clush and orterun are most
commonly used.

```bash
$ clush -w <server_list> -o "-t -t" daos_server start -o <config_file>`
```
will launch `daos_server` on the specified hosts connecting to the `port`
parameter value specified in the server config file.
If the number of storage node exceed the default fanout value, then "-f"
followed by the number of storage nodes should be used.

Similarly, pdsh can be used:
```bash
$ pdsh -w <server_list> daos_server start -o <config_file>`
```

As for orterun, the list of storage nodes can be specified on the command line
via the -H option. To start the DAOS server, run:
```bash
$ orterun --map-by node --mca btl tcp,self --mca oob tcp -np <num_servers>
-H <server_list> --enable-recovery daos_server start -o <config_file>
```
The --enable-recovery is required for fault tolerance to guarantee that
the fault of one server does not cause the others to be stopped.

The --allow-run-as-root option can be added to the command line to
allow the `daos_server` to run with root privileges on each storage
nodes (for example when needing to perform privileged tasks relating
to storage format). See the orterun(1) man page for additional options.

#### Systemd Integration

DAOS Server can be started as a systemd service. The DAOS Server
unit file is installed in the correct location when installing from RPMs.
The DAOS Server will be run as `daos-server` user which will be created
during RPM install.

If you wish to use systemd with a development build, you must copy the service
file from utils/systemd to `/usr/lib/systemd/system`. Once the file is copied
modify the ExecStart line to point to your in tree `daos_server` binary.

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

#### Kubernetes Pod

DAOS service integration with Kubernetes is planned and will be
supported in a future DAOS version.

## Hardware Provisioning

Once the DAOS server started, the storage and network can be configured on the
storage nodes via the dmg utility.

### SCM Preparation

This section addresses how to verify that Optane DC Persistent Memory
Module (DCPMM) is correctly installed on the storage nodes, and how to configure
it in interleaved mode to be used by DAOS in AppDirect mode.
Instructions for other types of SCM may be covered in the future.

Provisioning the SCM occurs by configuring DCPM modules in AppDirect memory regions
(interleaved mode) in groups of modules local to a specific socket (NUMA), and
resultant nvdimm namespaces are defined by a device identifier (e.g., /dev/pmem0).

DCPM preparation is required once per DAOS installation and
requires the DAOS Control Servers to be running as root.

This step requires a reboot to enable DCPM resource allocation
changes to be read by BIOS.

DCPM preparation can be performed from the management tool
`dmg storage prepare --scm-only` or using the Control Server directly
`sudo daos_server storage prepare --scm-only`.

The first time the command is run, the SCM AppDirect regions will be created as
resource allocations on any available DCPM modules (one region per NUMA
node/socket). The regions are activated after BIOS reads the new resource
allocations, and after initial completion the command prints a
message to ask for a reboot (the command will not initiate reboot itself).

After running the command a reboot will be required, then the Control
Servers will then need to be started again and the command run for a
second time to expose the namespace device to be used by DAOS.

Example usage:

- `dmg -l wolf-[118-121,130-133] -i storage prepare --scm-only`
after running, the user should be prompted for a reboot.

- `clush -w wolf-[118-121,130-133] reboot`

- `clush -w wolf-[118-121,130-133] daos_server start -o utils/config/examples/daos_server_sockets.yml`

- `dmg -l wolf-[118-121,130-133] -i storage prepare --scm-only`
after running, `/dev/pmemX` devices should be available on each of the hosts.

'sudo daos_server storage prepare --scm-only' should be run for a second time after
system reboot to create the pmem kernel devices (/dev/pmemX
namespaces created on the new SCM regions).

On the second run, one namespace per region is created, and each namespace may
take up to a few minutes to create. Details of the pmem devices will be
displayed in JSON format on command completion.

Example output from the initial call (with the SCM modules set to default MemoryMode):

```bash
Memory allocation goals for SCM will be changed and namespaces modified, this
will be a destructive operation.  ensure namespaces are unmounted and SCM is
otherwise unused.
A reboot is required to process new memory allocation goals.
```

Example output from the subsequent call (SCM modules configured to AppDirect
mode, and host rebooted):

```bash
Memory allocation goals for SCM will be changed and namespaces modified. This
will be a destructive operation. Ensure namespaces are unmounted and the SCM
is otherwise unused.
creating SCM namespace, may take a few minutes...
creating SCM namespace, may take a few minutes...
Persistent memory kernel devices:
[{UUID:5d2f2517-9217-4d7d-9c32-70731c9ac11e Blockdev:pmem1 Dev:namespace1.0 NumaNode:1} {UUID:2bfe6c40-f79a-4b8e-bddf-ba81d4427b9b Blockdev:pmem0 Dev:namespace0.0 NumaNode:0}]
```

Upon successful creation of the pmem devices, DCPMM is properly configured and
one can move on to the next step.

If required, the pmem devices can be destroyed via the --reset option:

`sudo daos_server [<app_opts>] storage prepare [--scm-only|-s] --reset [<cmd_opts>]`

All namespaces are disabled and destroyed. The SCM regions are removed by
resetting modules into "MemoryMode" through resource allocations.

Note that undefined behavior may result if the namespaces/pmem kernel
devices are mounted before running reset (as per the printed warning).

A subsequent reboot is required for BIOS to read the new resource
allocations.

Example output when resetting the SCM modules:

```bash
Memory allocation goals for SCM will be changed and namespaces modified, this
will be a destructive operation.  ensure namespaces are unmounted and SCM is
otherwise unused.
removing SCM namespace, may take a few minutes...
removing SCM namespace, may take a few minutes...
resetting SCM memory allocations
A reboot is required to process new memory allocation goals.
```



### Storage Selection

While the DAOS server auto-detects all the usable storage, the administrator
will still be provided with the ability through the configuration file
(see next section) to whitelist or blacklist the storage devices to be
(or not) used. This section covers how to manually detect the storage devices
potentially usable by DAOS to populate the configuration file when the
administrator wants to have finer control over the storage selection.

`dmg storage scan` can be run to query remote running `daos_server`
processes over the management network.

`sudo daos_server storage scan` can be used to query `daos_server`
directly (scans locally-attached SSDs and Intel Persistent Memory
Modules usable by DAOS).

```bash
[daos@wolf-72 daos_m]$ dmg -l wolf-7[1-2] -i storage scan --verbose
wolf-[71-72]:10001: connected
------------
wolf-[71-72]
------------
SCM Namespace Socket ID Capacity
------------- --------- --------
pmem0         0         2.90TB
pmem1         1         2.90TB

NVMe PCI     Model                FW Revision Socket ID Capacity
--------     -----                ----------- --------- --------
0000:81:00.0 INTEL SSDPED1K750GA  E2010325    1         750.00GB
0000:87:00.0 INTEL SSDPEDMD016T4  8DV10171    1         1.56TB
0000:da:00.0 INTEL SSDPED1K750GA  E2010325    1         750.00GB
```

The NVMe PCI field above is what should be used in the server
configuration file to identified NVMe SSDs.

Devices with the same NUMA node/socket should be used in the same per-server
section of the server configuration file for best performance.

Note that other storage query commands are also available,
`dmg storage --help` for listings.

SSD health state can be verified via `dmg storage query nvme-health`:

```bash
$ dmg -l wolf-71 storage query nvme-health
wolf-71:10001: connected
wolf-71:10001
        NVMe controllers and namespaces detail with health statistics:
                PCI:0000:81:00.0 Model:INTEL SSDPED1K750GA  FW:E2010325 Socket:1 Capacity:750TB
                Health Stats:
                        Temperature:288K(15C)
                        Controller Busy Time:5h26m0s
                        Power Cycles:4
                        Power On Duration:16488h0m0s
                        Unsafe Shutdowns:2
                        Media Errors:0
                        Error Log Entries:0
                        Critical Warnings:
                                Temperature: OK
                                Available Spare: OK
                                Device Reliability: OK
                                Read Only: OK
                                Volatile Memory Backup: OK
```

The next step consists of adjusting in the server configuration the storage
devices that should be used by DAOS. The `servers` section of the yaml is a
list specifying details for each DAOS I/O instance to be started on the host
(currently a maximum of 2 per host is imposed).

Devices with the same NUMA rating/node/socket should be colocated on
a single DAOS I/O instance where possible.
[more details](#server-configuration)

- `bdev_list` should be populated with NVMe PCI addresses
- `scm_list` should be populated with DCPM interleaved set namespaces
(e.g. `/dev/pmem1`)
- DAOS Control Servers will need to be restarted on all hosts after
updates to the server configuration file.
- Pick one host in the system and set `access_points` to list of that
host's hostname or IP address (don't need to specify port).
This will be the host which bootstraps the DAOS management service
(MS).

To illustrate, assume a cluster with homogenous hardware
configurations that returns the following from scan for each host:

```bash
[daos@wolf-72 daos_m]$ dmg -l wolf-7[1-2] -i storage scan --verbose
wolf-7[1-2]:10001: connected
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

In this situation, the configuration file `servers` section could be
populated as follows:

```yaml
<snip>
port: 10001
access_points: ["wolf-71"] # <----- updated
<snip>
servers:
-
  targets: 8                # count of storage targets per each server
  first_core: 0             # offset of the first core for service xstreams
  nr_xs_helpers: 2          # count of offload/helper xstreams per target
  fabric_iface: eth0        # map to OFI_INTERFACE=eth0
  fabric_iface_port: 31416  # map to OFI_PORT=31416
  log_mask: ERR             # map to D_LOG_MASK=ERR
  log_file: /tmp/server.log # map to D_LOG_FILE=/tmp/server.log
  env_vars:                 # influence DAOS IO Server behaviour by setting env variables
  - DAOS_MD_CAP=1024
  - CRT_CTX_SHARE_ADDR=0
  - CRT_TIMEOUT=30
  - FI_SOCKETS_MAX_CONN_RETRY=1
  - FI_SOCKETS_CONN_TIMEOUT=2000
  scm_mount: /mnt/daos  # map to -s /mnt/daos
  scm_class: dcpm
  scm_list: [/dev/pmem0] # <----- updated
  bdev_class: nvme
  bdev_list: ["0000:87:00.0", "0000:81:00.0"]  # <----- updated
-
  targets: 8                # count of storage targets per each server
  first_core: 0             # offset of the first core for service xstreams
  nr_xs_helpers: 2          # count of offload/helper xstreams per target
  fabric_iface: eth0        # map to OFI_INTERFACE=eth0
  fabric_iface_port: 31416  # map to OFI_PORT=31416
  log_mask: ERR             # map to D_LOG_MASK=ERR
  log_file: /tmp/server.log # map to D_LOG_FILE=/tmp/server.log
  env_vars:                 # influence DAOS IO Server behaviour by setting env variables
  - DAOS_MD_CAP=1024
  - CRT_CTX_SHARE_ADDR=0
  - CRT_TIMEOUT=30
  - FI_SOCKETS_MAX_CONN_RETRY=1
  - FI_SOCKETS_CONN_TIMEOUT=2000
  scm_mount: /mnt/daos  # map to -s /mnt/daos
  scm_class: dcpm
  scm_list: [/dev/pmem1] # <----- updated
  bdev_class: nvme
  bdev_list: ["0000:da:00.0"]  # <----- updated
<end>
```

### Network Configuration

To display the fabric interface, OFI provider and NUMA node
combinations detected on the DAOS server, use the following command:
```bash
$ daos_server network scan --all

        fabric_iface: ib0
        provider: ofi+psm2
        pinned_numa_node: 0

        fabric_iface: ib1
        provider: ofi+psm2
        pinned_numa_node: 1

        fabric_iface: ib0
        provider: ofi+verbs;ofi_rxm
        pinned_numa_node: 0

        fabric_iface: ib1
        provider: ofi+verbs;ofi_rxm
        pinned_numa_node: 1

        fabric_iface: ib0
        provider: ofi+verbs
        pinned_numa_node: 0

        fabric_iface: ib1
        provider: ofi+verbs
        pinned_numa_node: 1

        fabric_iface: ib0
        provider: ofi+sockets
        pinned_numa_node: 0

        fabric_iface: ib1
        provider: ofi+sockets
        pinned_numa_node: 1

        fabric_iface: eth0
        provider: ofi+sockets
        pinned_numa_node: 0

        fabric_iface: lo
        provider: ofi+sockets
        pinned_numa_node: 0
```
The network scan leverages data from libfabric.  Results are ordered from
highest performance at the top to lowest performance at the bottom of the list.
Once the fabric_iface and provider pair has been chosen, those items and the
pinned_numa_node may be inserted directly into the corresponding sections within
daos_server.yml. Note that the provider is currently the same for all DAOS
IO server instances and is configured once in the server configuration.
The fabric_iface and pinned_numa_node are configured for each IO server
instance.

A list of providers that may be querried is found with the command:
```bash
$ daos_server network list

Supported providers:
        ofi+gni, ofi+psm2, ofi+tcp, ofi+sockets, ofi+verbs, ofi_rxm
```

Performing a network scan that filters on a specific provider is accomplished
by issuing the following command:
```bash
$ daos_server network scan --provider 'ofi+verbs;ofi_rxm'

Scanning fabric for cmdline specified provider: ofi+verbs;ofi_rxm
Fabric scan found 2 devices matching the provider spec: ofi+verbs;ofi_rxm

        fabric_iface: ib0
        provider: ofi+verbs;ofi_rxm
        pinned_numa_node: 0


        fabric_iface: ib1
        provider: ofi+verbs;ofi_rxm
        pinned_numa_node: 1
```
To aid in provider configuration and debug, it may be helpful to run the
fi_pingpong test (delivered as part of OFI/libfabric).  To run that test,
determine the name of the provider to test usually by removing the "ofi+" prefix from the network scan provider data.  Do use the "ofi+" prefix in the
daos_server.yml.  Do not use the "ofi+" prefix with fi_pingpong.

Then, the fi_pingpong test can be used to verify that the targeted OFI provider works fine:
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

## Storage Formatting

Once the `daos_server` has been restarted with the correct storage devices and
network interface to use, one can move to the format phase.
When `daos_server` is started for the first time, it enters "maintenance mode"
and waits for a `dmg storage format` call to be issued from the management tool.
This remote call will trigger the formatting of the locally attached storage on
the host for use with DAOS using the parameters defined in the server config file.

`dmg -i -l <host:port>[,...] storage format` will normally be run on a login
node specifying a hostlist (`-l <host:port>[,...]`) of storage nodes with SCM/DCPM
modules and NVMe SSDs installed and prepared.

Upon successful format, DAOS Control Servers will start DAOS IO
instances that have been specified in the server config file.

Successful start-up is indicated by the following on stdout:
`DAOS I/O server (v0.8.0) process 433456 started on rank 1 with 8 target, 2 helper XS per target, firstcore 0, host wolf-72.wolf.hpdd.intel.com.`

### SCM Format

When the command is run, the pmem kernel devices created on SCM/DCPM regions are
formatted and mounted based on the parameters provided in the server config file.

- `scm_mount` specifies the location of the mountpoint to create.
- `scm_class` can be set to `ram` to use a tmpfs in the situation that no SCM/DCPM
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
IO services if valid DAOS metadata is found in `scm_mount`.

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
(`/usr/etc/daos_agent.yml`).

Parameter descriptions are specified in [daos_agent.yml](https://github.com/daos-stack/daos/blob/master/utils/config/daos_agent.yml).

Any option supplied to `daos_agent` as a command line option or flag will
take precedence over equivalent configuration file parameter.

For convenience, active parsed config values are written to a temporary
file for reference, and the location will be written to the log.

The following section lists the format, options, defaults, and
descriptions available in the configuration file.

The example configuration file lists the default empty configuration listing all the
options (living documentation of the config file). Live examples are
available at
<https://github.com/daos-stack/daos/tree/master/utils/config>

The location of this configuration file is determined by first checking
for the path specified through the -o option of the daos_agent command
line. Otherwise, /etc/daos_agent.conf is used.

Refer to the example configuration file ([daos_server.yml](https://github.com/daos-stack/daos/blob/master/utils/config/daos_server.yml))
for latest information and examples.

### Agent Startup

DAOS Agent is a standalone application to be run on each compute node.
It can be configured to use secure communications (default) or can be allowed
to communicate with the control plane over unencrypted channels. The following
example shows daos_agent being configured to operate in insecure mode due to
incomplete integration of certificate support as of the 0.6 release and
configured to use a non-default agent configuration file.

To start the DAOS Agent from the command line, run:

```bash
$ daos_agent -i -o <'path to agent configuration file/daos_agent.yml'> &
```

Alternatively, the DAOS Agent can be started as a systemd service. The DAOS Agent
unit file is installed in the correct location when installing from RPMs.

If you wish to use systemd with a development build, you must copy the service
file from `utils/systemd` to `/usr/lib/systemd/system`. Once the file is copied
modify the ExecStart line to point to your in tree `daos_agent` binary.

`ExecStart=/usr/bin/daos_agent -i -o <'path to agent configuration file/daos_agent.yml'>`

Once the service file is installed, you can start `daos_agent`
with the following commands:

```bash
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

## System Validation

To validate that the DAOS system is properly installed, the `daos_test`
suite can be executed. Ensure the DAOS Agent is configured before running
`daos_test` and that the following environment variables are properly set:

- `CRT_PHY_ADDR_STR` must be set to match the provider specified in the server
  yaml configuration file (e.g. export `CRT_PHY_ADDR_STR="ofi+sockets"`)

- `OFI_INTERFACE` is set to the network interface you want to user on the client
  node.

- `OFI_DOMAIN` must optionally be set for infiniband deployments.

- `DAOS_AGENT_DRPC_DIR` must also optionally be set if the agent is using a
  non-default path for the socket.

While those environment variables need to be set up for DAOS v1.0, versions 1.2
and upward automatically set up the environment via the agent and don't require
any special environment set up to run applications.

```bash
mpirun -np <num_clients> --hostfile <hostfile> ./daos_test
```

daos_test requires at least 8GB of SCM (or DRAM with tmpfs) storage on
each storage node.

[^1]: https://github.com/intel/ipmctl

[^2]: https://github.com/daos-stack/daos/tree/master/utils/config

[^3]: [*https://www.open-mpi.org/faq/?category=running\#mpirun-hostfile*](https://www.open-mpi.org/faq/?category=running#mpirun-hostfile)

[^4]: https://github.com/daos-stack/daos/tree/master/src/control/README.md
