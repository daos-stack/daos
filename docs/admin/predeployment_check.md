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

## Setup for multiple network links

With libfabric, it is not supported to aggregate (or stripe over) multiple
network links as a single endpoint. So a single DAOS engine can only use
a single network link.
But when a storage node runs multiple engine instances, each engine instance
can and should manage its own network link to achieve optimum performance.

### Subnet

Since all engines need to be able to communicate, the different network
interfaces must be on the same subnet or you must configuring routing
across the different subnets.

### Infiniband Settings

Some special configuration is required to configure the IP layer and
to use librdmacm with multiple interfaces in the same IP network.

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
sysctl file under /etc/sysctl.d (e.g. /etc/sysctl.d/95-daos-net.conf) with all
the relevant settings.

For more information, please refer to the [librdmacm documentation](https://github.com/linux-rdma/rdma-core/blob/master/Documentation/librdmacm.md)

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

-   Copy the config file:
    `cp utils/systemd/daosfiles.conf /etc/tmpfiles.d`

-   Modify the copied file to change the user and group fields
    (currently daos) to the user daos will be run as

-   Reboot the system, and the directories will be created automatically
    on all subsequent reboots.

### Privileged Helper

DAOS employs a privileged helper binary (`daos_admin`) to perform tasks
that require elevated privileges on behalf of `daos_server`.


When DAOS is installed from RPM, the `daos_admin` helper is automatically installed
to the correct location with the correct permissions. The RPM creates a `daos_server`
system group and configures permissions such that `daos_admin` may only be invoked
from `daos_server`.

For non-RPM installations, there are two supported scenarios:

1. `daos_server` is run as root, which means that `daos_admin` is also invoked as root,
and therefore no additional setup is necessary.
2. `daos_server` is run as a non-root user, which means that `daos_admin` must be
manually installed and configured.

The steps to enable the second scenario are as follows (steps are assumed to be
running out of a DAOS source tree which may be on a NFS share):

```bash
$ chmod -x $daospath/bin/daos_admin # prevent this copy from being executed
$ sudo cp $daospath/bin/daos_admin /usr/bin/daos_admin
$ sudo chmod 4755 /usr/bin/daos_admin # make this copy setuid root
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

!!! note
    The RPM installation is preferred for production scenarios. Manual
    installation is most appropriate for development and predeployment
    proof-of-concept scenarios.

### Memory Lock Limits

Low ulimit for memlock can cause SPDK to fail and emit the following error:

```bash
daos_engine:1 EAL: cannot set up DMA remapping, error 12 (Cannot allocate
    memory)
```

The memlock limit only needs to be manually adjusted when `daos_server` is not
running as a systemd service. Default ulimit settings vary between OSes.

For RPM installations, the service will typically be launched by systemd and
the limit is pre-set to unlimited in the `daos_server.service`
[unit file](https://github.com/daos-stack/daos/blob/master/utils/systemd/daos_server.service)

Note that values set in `/etc/security/limits.conf` are ignored by services
launched by systemd.

For non-RPM installations where `daos_server` is launched directly from the
commandline (including source builds), limits should be adjusted in
`/etc/security/limits.conf` as per
[this article](https://access.redhat.com/solutions/61334) (which is a RHEL
specific document but the instructions apply to most Linux distributions).
