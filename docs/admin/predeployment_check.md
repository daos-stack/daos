# Pre-deployment Checklist

This section covers the preliminary setup required on the compute and
storage nodes before deploying DAOS.

## Enable IOMMU

In order to run the DAOS server as a non-root user with NVMe devices, the hardware
must support virtualized device access, and it must be enabled in the system BIOS.
On Intel® systems, this capability is named Intel® Virtualization Technology for
Directed I/O (VT-d). Once enabled in BIOS, IOMMU support must also be enabled in
the Linux kernel. Exact details depend on the distribution, but the following
example should be illustrative:

```bash
# Enable IOMMU on CentOS 7 and EL 8
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

!!! note
    To force SPDK to use UIO rather than VFIO at daos_server runtime, set
    'disable_vfio' in the [server config file](https://github.com/daos-stack/daos/blob/master/utils/config/daos_server.yml#L109),
    but note that this will require running daos_server as root.

!!! warning
    If VFIO is not enabled on RHEL 8.x and derivatives, you will run into the issue described in:
    https://github.com/spdk/spdk/issues/1153

    The problem manifests with the following signature in the kernel logs:

    ```
    [82734.333834] genirq: Threaded irq requested with handler=NULL and !ONESHOT for irq 113
    [82734.341761] uio_pci_generic: probe of 0000:18:00.0 failed with error -22
    ```

    As a consequence, the use of VFIO on these distributions is a requirement
    since UIO is not supported.

## Time Synchronization

The DAOS transaction model relies on timestamps and requires time to be
synchronized across all the storage nodes. This can be done using NTP or
any other equivalent protocol.

## User and Group Management

### DAOS User/Groups on the Servers

The `daos_server` and `daos_engine` processes run under a non-privileged userid `daos_server`.
If that user does not exist at the time the `daos-server` RPM is installed, the user will be
created as part of the RPM installation. A group `daos-server` will also be created as its
primary group, as well as two additional groups `daos_metrics` and `daos_daemons` to which
the `daos_server` user will be added.

If there are site-specific rules for the creation of users and groups, it is advisable to
create these users and groups following the site-specific conventions _before_ installing the
`daos-server` RPM.

### DAOS User/Groups on the Clients

The `daos_agent` process runs under a non-privileged userid `daos_agent`.
If that user does not exist at the time the `daos-client` RPM is installed, the user will be
created as part of the RPM installation. A group `daos-agent` will also be created as its
primary group, as well as an additional group `daos_daemons` to which the `daos_agent` user
will be added.

If there are site-specific rules for the creation of users and groups, it is advisable to
create these users and groups following the site-specific conventions _before_ installing the
`daos-client` RPM.

### User/Group Synchronization for End Users

DAOS ACLs for pools and containers store the actual user and group names (instead of numeric
IDs). Therefore the servers do not need access to a synchronized user/group database.
The DAOS Agent (running on the client nodes) is responsible for resolving a user's
UID/GID to user/group names, which are then added to a signed credential and sent to
the DAOS storage nodes.

## HPC Fabric setup

DAOS depends on the HPC fabric software stack and drivers. Depending on the type of HPC fabric
that is used, a supported version of the fabric stack needs to be installed.

Note that for InfiniBand fabrics, DAOS is only supported with the MLNX\_OFED stack that is
provided by NVIDIA, not with the distros' inbox drivers.
Before installing DAOS, a supported version of MOFED needs to be installed on the DAOS servers
and DAOS clients. If the control plane communication is set up over the InfiniBand fabric using
IPoIB, then any dedicated DAOS admin nodes should also be installed with the same MOFED stack.
This is typically done using the `mlnxofedinstall` command that is included with the MOFED
distribution.

### Multi-rail/NIC Setup

Storage nodes can be configured with multiple network interfaces to run
multiple engine instances.

#### Subnet

Since all engines need to be able to communicate, the different network
interfaces must be on the same subnet or you must configuring routing
across the different subnets.

#### Interface Settings

Some special configuration is required for the `verbs` provider to use librdmacm
with multiple interfaces, and the same configuration is required for the `tcp` provider.

First, the accept_local feature must be enabled on the network interfaces
to be used by DAOS. This can be done using the following command:

```
$ sudo sysctl -w net.ipv4.conf.all.accept_local=1
```

Second, Linux must be configured to only send ARP replies on the interface
targeted in the ARP request. This is configured via the arp_ignore parameter.
This should be set to 2 if all the IPoIB interfaces on the client and storage
nodes are in the same logical subnet (e.g. ib0 == 10.0.0.27, ib1 == 10.0.1.27,
prefix=16).

```
$ sysctl -w net.ipv4.conf.all.arp_ignore=2
```

If separate logical subnets are used (e.g. prefix = 24), then the value must be
set to 1.

```
$ sysctl -w net.ipv4.conf.all.arp_ignore=1
```

Finally, the rp_filter is set to 1 by default on several distributions (e.g. on
CentOS 7 and EL 8) and should be set to either 0 or 2, with 2 being more secure. This is
true even if the configuration uses a single logical subnet. <ifaces> must be replaced with
the interface names)

```
$ sysctl -w net.ipv4.conf.<ifaces>.rp_filter=2
```

All those parameters can be made persistent in /etc/sysctl.conf by adding a new
sysctl file under /etc/sysctl.d (e.g. /etc/sysctl.d/95-daos-net.conf)
with all the relevant settings.

For more information, please refer to the [librdmacm documentation](https://github.com/linux-rdma/rdma-core/blob/master/Documentation/librdmacm.md)

### Firewall

Some distributions install a firewall as part of the base OS installation. DAOS uses port 10001
(or whatever is configured as the `port:` in the configuration files in /etc/daos)
for its management service. If this port is blocked by firewall rules, neither `dmg` nor the
`daos_agent` on a remote node will be able to contact the DAOS server(s).

Either configure the firewall to allow traffic for this port, or disable the firewall
(for example, by running `systemctl stop firewalld; systemctl disable firewalld`).

## Install from Source

When DAOS is installed from source (and not from pre-built packages), extra manual
settings detailed in this section are required.

### Runtime Directory Setup

DAOS uses a series of Unix Domain Sockets to communicate between its
various components. On modern Linux systems, Unix Domain Sockets are
typically stored under /run or /var/run (usually a symlink to /run) and
are a mounted tmpfs file system. There are several methods for ensuring
the necessary directories are setup.

A sign that this step may have been missed is when starting daos_server
or daos_agent, you may see the message:
```bash
$ mkdir /var/run/daos_server: permission denied
Unable to create socket directory: /var/run/daos_server
```
#### Non-default Directory

By default, daos_server and daos_agent will use the directories
/var/run/daos_server and /var/run/daos_agent respectively. To change
the default location that daos_server uses for its runtime directory,
uncomment and set the socket_dir configuration value in /etc/daos/daos_server.yml.
For the daos_agent, either uncomment and set the runtime_dir configuration value in
/etc/daos/daos_agent.yml or a location can be passed on the command line using
the --runtime_dir flag (`daos_agent -d /tmp/daos_agent`).

!!! warning
    Do not change these when running under `systemd` control.
    If these directories need to be changed, insure they match the
    RuntimeDirectory setting in the /usr/lib/systemd/system/daos_agent.service
    and /usr/lib/systemd/system/daos_server.service configuration files.
    The socket directories will be created and removed by `systemd` when the
    services are started and stopped.

#### Default Directory (non-persistent)

Files and directories created in /run and /var/run only survive until
the next reboot. These directories are required for subsequent runs;
therefore, if reboots are infrequent, an easy solution
while still utilizing the default locations is to create the
required directories manually. To do this execute the following commands.

daos_server:
```bash
$ mkdir /var/run/daos_server
$ chmod 0755 /var/run/daos_server
$ chown user:user /var/run/daos_server (where user is the user you
    will run daos_server as)
```
daos_agent:
```bash
$ mkdir /var/run/daos_agent
$ chmod 0755 /var/run/daos_agent
$ chown user:user /var/run/daos_agent (where user is the user you
    will run daos_agent as)
```

#### Default Directory (persistent)

The following steps are not necessary if DAOS is installed from rpms.

If the server hosting `daos_server` or `daos_agent` will be rebooted often,
systemd provides a persistent mechanism for creating the required
directories called tmpfiles.d. This mechanism will be required every
time the system is provisioned and requires a reboot to take effect.

To tell systemd to create the necessary directories for DAOS:

-   Copy the file utils/systemd/daosfiles.conf to /etc/tmpfiles.d\
    cp utils/systemd/daosfiles.conf /etc/tmpfiles.d

-   Modify the copied file to change the user and group fields
    (currently daos) to the user daos will be run as

-   Reboot the system, and the directories will be created automatically
    on all subsequent reboots.

### Privileged Helper

DAOS employs a privileged helper binary (`daos_server_helper`) to perform tasks
that require elevated privileges on behalf of `daos_server`.


When DAOS is installed from RPM, the `daos_server_helper` helper is automatically installed
to the correct location with the correct permissions. The RPM creates a "daos_server"
system group and configures permissions such that `daos_server_helper` may only be invoked
from `daos_server`.

For non-RPM installations, there are two supported scenarios:

1. `daos_server` is run as root, which means that `daos_server_helper` is also invoked as root,
and therefore no additional setup is necessary.
2. `daos_server` is run as a non-root user, which means that `daos_server_helper` must be
manually installed and configured.

The steps to enable the second scenario are as follows (steps are assumed to be
running out of a DAOS source tree which may be on a NFS share):

```bash
$ chmod -x $daospath/bin/daos_server_helper # prevent this copy from being executed
$ sudo cp $daospath/bin/daos_server_helper /usr/bin/daos_server_helper
$ sudo chmod 4755 /usr/bin/daos_server_helper # make this copy setuid root
$ sudo mkdir -p /usr/share/daos/control # create symlinks to SPDK scripts
$ sudo ln -sf $daospath/share/daos/control/setup_spdk.sh \
           /usr/share/daos/control
$ sudo mkdir -p /usr/share/spdk/scripts
$ sudo ln -sf $daospath/share/spdk/scripts/setup.sh \
           /usr/share/spdk/scripts
$ sudo ln -sf $daospath/share/spdk/scripts/common.sh \
           /usr/share/spdk/scripts
$ sudo ln -s $daospath/include \
           /usr/share/spdk/include
```

For convenience, the `utils/setup_daos_server_helper.sh` script may be used to automate the steps
described above.

!!! note
    The RPM installation is preferred for production scenarios. Manual
    installation is most appropriate for development and predeployment
    proof-of-concept scenarios.

### Memory Lock Limits

Several components of the DAOS software stack may require to increase the
`RLIMIT_MEMLOCK` limit from its system default.

On the DAOS servers, a low ulimit for `memlock` can cause SPDK to fail and emit the following error:

```bash
daos_engine:1 EAL: cannot set up DMA remapping, error 12 (Cannot allocate memory)
```

On both DAOS servers and DAOS clients, the fabric stack (libfabric/verbs, UCX, ...)
also needs some amount of memlock'ed address space. For a large number of clients
and/or a large number of targets per engine, the default memlock limits may cause
failures.

For RPM installations, the `daos_server` and `daos_agent` services will typically be
launched by `systemd` and its `LimitMEMLOCK` limit is set to `infinity` in the
[`daos_server.service`](https://github.com/daos-stack/daos/blob/master/utils/systemd/daos_server.service)
and
[`daos_agent.service`](https://github.com/daos-stack/daos/blob/master/utils/systemd/daos_agent.service)
unit files.
(Note that values set in `/etc/security/limits.conf` are ignored by services
launched through `systemd`.)

When `daos_server` and/or `daos_agent` are not run as a `systemd` service,
the `memlock` ulimit should be manually set to unlimited. This applies to both
RPM installations when `systemd` is not used, as well as to non-RPM installations
(including source builds) where `daos_server` and/or `daos_agent` are launched
directly from the commandline.

Limits should be adjusted in `/etc/security/limits.conf` as per
[this article](https://access.redhat.com/solutions/61334) (which is a RHEL
specific document, but the instructions apply to most Linux distributions).

### Memory mapped areas

The DAOS engine heavily uses mmap(2) to access persistent memory and to
allocate stacks for asynchronous request processing via Argobots.
The Linux kernel imposes a maximum limit (i.e. `vm.max_map_count`) on the
number of mmap regions that a process can create.

Low max number of per-process mapped areas (`vm.max_map_count`) can cause ULT
stack allocation to fall-back from DAOS mmap()'ed way into Argobots preferred
allocation method.

The `vm.max_map_count` default value (65530) needs to be bumped to a much more higher
value (1M) to better fit with the DAOS needs for the expected huge number of
concurrent ULTs if we want all their stacks to be mmap()'ed.

For RPM installations, `vm.max_map_count` is raised through installed
`/etc/sysctl.d/10-daos_server.conf` file.

For non-RPM installations, `vm.max_map_count` may need to be bumped (usual default
of 65530 is too low for non-testing configurations), and the best way to do
so is to copy `utils/rpms/10-daos_server.conf` into `/etc/sysctl.d/`
to apply the setting automatically on boot.
Running `/usr/lib/systemd/systemd-sysctl /etc/sysctl.d/10-daos_server.conf`
will apply these settings immediately (avoiding the need for an immediate reboot).

## Socket receive buffer size

Low socket receive buffer size can cause SPDK to fail and emit the following
error (receive buffer size is required to be above 1MB):

```bash
daos_engine:1 pci_event.c:  68:spdk_pci_event_listen: *ERROR*: Failed to set socket
 option
```

The socket receive buffer size does not need to be manually adjusted if
`daos_server` has been installed using an RPM package (as the settings
will be applied automatically on install).

For non-RPM installations where `daos_server` has been built from source,
`rmem_default` and `rmem_max` settings should be set to >= 1MB.
Optionally, the `utils/rpms/10-daos_server.conf` can be copied to `/etc/sysctl.d/`
to apply the settings automatically on boot.
Running `/usr/lib/systemd/systemd-sysctl /etc/sysctl.d/10-daos_server.conf`
will apply these settings immediately (avoiding the need for an immediate reboot).
For further information see
[this article on network kernel settings](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/5/html/tuning_and_optimizing_red_hat_enterprise_linux_for_oracle_9i_and_10g_databases/sect-oracle_9i_and_10g_tuning_guide-adjusting_network_settings-changing_network_kernel_settings)
using any of the methods described in
[this article on adjusting kernel tunables](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/7/html/kernel_administration_guide/working_with_sysctl_and_kernel_tunables).

## Optimize NVMe SSD Block Size

DAOS server performs NVMe I/O in 4K granularity so in order to avoid alignment
issues it is beneficial to format the SSDs that will be used with a 4K block size.

First the SSDs need to be bound to a user-space driver to be usable with SPDK, to do
this, use the SPDK setup script.

`setup.sh` script is provided by SPDK and will be found in the following locations:
- `/usr/share/spdk/scripts/setup.sh` if DAOS-maintained spdk-tools-21.07 (or greater) RPM
is installed
- `<daos_src>/install/share/spdk/scripts/setup.sh` after build from DAOS source

Bind the SSDs with the following commands:
```bash
$ sudo /usr/share/spdk/scripts/setup.sh
0000:01:00.0 (8086 0953): nvme -> vfio-pci
"daos" user memlock limit: 2048 MB

This is the maximum amount of memory you will be
able to use with DPDK and VFIO if run as user "daos".
To change this, please adjust limits.conf memlock limit for user "daos".
```

Now the SSDs can be accessed by SPDK we can use the `spdk_nvme_manage` tool to format
the SSDs with a 4K block size.

`spdk_nvme_manage` tool is provided by SPDK and will be found in the following locations:
- `/usr/bin/spdk_nvme_manage` if DAOS-maintained spdk-21.07-10 (or greater) RPM is installed
- `<daos_src>/install/prereq/release/spdk/bin/spdk_nvme_manage` after build from DAOS source

Choose to format a SSD, use option "6" for formatting:
```bash
$ sudo /usr/bin/spdk_nvme_manage
NVMe Management Options
[1: list controllers]
[2: create namespace]
[3: delete namespace]
[4: attach namespace to controller]
[5: detach namespace from controller]
[6: format namespace or controller]
[7: firmware update]
[8: quit]
6
```

Available SSDs will then be listed and you will be prompted to select one.

Select the SSD to format, enter PCI Address "01:00.00":
```bash
0000:01:00.00 INTEL SSDPEDMD800G4 CVFT45050002800CGN 0
Please Input PCI Address(domain:bus:dev.func):
01:00.00
```

Erase settings will be displayed and you will be prompted to select one.

Erase the SSD using option "0":
```bash
Please Input Secure Erase Setting:
0: No secure erase operation requested
1: User data erase
2: Cryptographic erase
0
```

Supported LBA formats will then be displayed and you will be prompted to select one.

Format the SSD into 4KB block size using option "3".
```bash
Supported LBA formats:
0: 512 data bytes
1: 512 data bytes + 8 metadata bytes
2: 512 data bytes + 16 metadata bytes
3: 4096 data bytes
4: 4096 data bytes + 8 metadata bytes
5: 4096 data bytes + 64 metadata bytes
6: 4096 data bytes + 128 metadata bytes
Please input LBA format index (0 - 6):
3
```

A warning will be displayed and you will be prompted to confirm format action.

Confirm format request by entering "Y":
```bash
Warning: use this utility at your own risk.
This command will format your namespace and all data will be lost.
This command may take several minutes to complete,
so do not interrupt the utility until it completes.
Press 'Y' to continue with the format operation.
Y
```

Format will now proceed and a reset notice will be displayed for the given SSD.

Format is complete if you see something like the following:
```bash
[2022-01-04 12:56:30.075104] nvme_ctrlr.c:1414:nvme_ctrlr_reset: *NOTICE*: [0000:01:00.0] resetting
controller
press Enter to display cmd menu ...
<enter>
```

Once formats has completed, verify LBA format has been applied as expected.

Choose to list SSD controller details, use option "1":
```bash
NVMe Management Options
[1: list controllers]
[2: create namespace]
[3: delete namespace]
[4: attach namespace to controller]
[5: detach namespace from controller]
[6: format namespace or controller]
[7: firmware update]
[8: quit]
1
```

Controller details should show new "Current LBA Format".

Verify "Current LBA Format" is set to "LBA Format #03":
```bash
=====================================================
NVMe Controller:        0000:01:00.00
============================
Controller Capabilities/Features
Controller ID:          0
Serial Number:          CVFT550400F4800HGN

Admin Command Set Attributes
============================
Namespace Manage And Attach:            Not Supported
Namespace Format:                       Supported

NVM Command Set Attributes
============================
Namespace format operation applies to all namespaces

Namespace Attributes
============================
Namespace ID:1
Size (in LBAs):              195353046 (186M)
Capacity (in LBAs):          195353046 (186M)
Utilization (in LBAs):       195353046 (186M)
Format Progress Indicator:   Not Supported
Number of LBA Formats:       7
Current LBA Format:          LBA Format #03
...
```

Displayed details for controller show LBA format is now "#03".

Perform the above process for all SSDs that will be used by DAOS.
