# High-Speed Fabric Support

## Control Plane and Data Plane

DAOS uses two types of network communication between nodes:

* Management traffic in the DAOS _control plane_. The control plane uses
  gRPC/TCP for communication between servers, from the DAOS admin nodes to all
  DAOS servers (through the `dmg` command and the DAOS management API), and
  from the `daos_agent` daemon on the DAOS client nodes to all DAOS servers
  (but not to other clients).

* Application I/O in the DAOS _data plane_.
  The DAOS data plane uses the DAOS Collective and RPC Transport (CaRT) layer
  for communication between DAOS clients and DAOS servers.
  CaRT relies on the [Mercury](https://mercury-hpc.github.io/user/overview/)
  RPC framework, which provides RDMA transfer capabilities if the underlying
  high performance fabric supports it.

When designing a DAOS solution, one question that needs to be decided
early on is if the control plane traffic will be using the same physical
network as the data plane (for example, and IP-over-IB interface on the
InfiniBand NIC in an InfiniBand network), or if the control plane will use
a separate physical network (like an administrative Ethernet network).

* For example, if there is a reliable 25GbE management network over which
  all DAOS servers, DAOS clients and DAOS admin nodes can communicate,
  then it is possible to use that network for the control plane.

* On the other hand, if the management network of the DAOS servers is isolated
  from the client nodes' management network (which is a best practice to
  protect the server-side management interfaces), then the control plane
  must use TCP over the data plane network to guarantee that the DAOS
  servers, clients and admin nodes can communicate with each other.

The control plane network is configured in the `daos_server.yml`,
`daos_agent.yml` and
`daos_control.yml` files, by using the IP addresses (or hostnames) of
either then nodes' management network interface or an IP-over-IB interface
of their high-speed NICs in the control plane configuration sections.
The settings in these three configuration files must be consistent.

Given an 8-node cluster of nodes n[0001-0008], where:

- nodes n[0001,0004,0008] are defined as management service replicas, and
- the '-ibs1' node name suffix represents and IP-over-IB interface,

the following example would designate a control plane network using
the IP-over-IB interface:

```yaml
# daos_agent.yml
mgmt_svc_replicas:
- n0001-ibs1
- n0004-ibs1
- n0008-ibs1

# daos_agent.yml
access_points:
- n0001-ibs1
- n0004-ibs1
- n0008-ibs1

# daos_control.yml
hostlist:
- n0001-ibs1
- n0002-ibs1
- n0003-ibs1
- n0004-ibs1
- n0005-ibs1
- n0006-ibs1
- n0007-ibs1
- n0008-ibs1
```

!!! note
    On dual-socket DAOS servers with two InfiniBand NICs, the second InfiniBand
    NIC also has an IP-over-IB interface, address and name (which is used by the
    DAOS engines).
    By default the DAOS control plane (`daos_server` process)
    binds to 0.0.0.0, so it listens on all network interfaces.
    To limit the control plane to a specific interface, the `control_iface:`
    setting in the `daos_server.yml` configuration file can be used.

To use the management network interface, replace the above hostnames with the
hostnames of the management network interfaces.

The rest of this document assumes that the control plane is using
IP-over-IB over the data plane's high-speed NICs,
and focuses on data plane configuration.

Please refer to
[Partition isolation for control plane traffic over InfiniBand)](ib-pkey.md)
for important control plane configuration information regarding
InfiniBand networks.

## Mercury installation

The CaRT layer that is used by the DAOS _data plane_ is part of the main
DAOS RPM packages. CaRT uses the Mercury RPC framework, which is provided
as separate RPMs within the DAOS packages directory.
For NVIDIA based InfiniBand and RoCE Ethernet fabrics, Mercury's
[UCX](https://openucx.org/) backend is the recommended plugin.
For all other fabrics, Mercury's
[libfabric](https://ofiwg.github.io/libfabric/) backend is used.
DAOS Version 2.8 uses Mercury Version 2.4.1.

Mercury backends are dynamically loaded, and there is no RPM dependency
in the base `mercury` RPM for a specific backend. Depending on the intended
fabric provider, the corresponding Mercury backend RPM (`mercury-libfabric`
or `mercury-ucx`) must be explicitly installed.
(The base `mercury` RPM will be automatically installed,
because the DAOS RPMs have an RPM dependency on `mercury`).
It is possible to install both Mercury backends on the same node.

Failing to install the Mercury backend that corresponds to the fabric
provider configured in `daos_server.yml` will result in a runtime error,
and DAOS will not start.
For example, when a libfabric provider is used but the `mercury-libfabric`
RPM is not installed, an error similar to this will be reported
in the DAOS server log:

```
na.c:570 na_plugin_scan_path() Could not open plugin (libna_plugin_ofi.so)
```

The Mercury backend RPMs have dependencies on libfabric and UCX,
respectively, and it is recommended to install the fabric's
host software stack before installing DAOS to satisfy these dependencies.

## Fabric host software stack installation

Each high performance fabric typically provides its own host software stack.
To get support from the fabric vendor in case of networking issues,
it is vital that a supported version of the vendor-provided host software
stack is used. Linux distributions' "inbox" versions are often outdated
and vendor support will not be available until the system is
updated to recent levels of the vendor-provided hosts software stack.

This means that for NVIDIA-provided fabrics a recent version
of the DOCA-OFED stack must be installed, which will provide the UCX
libraries that are required by the `mercury-ucx` backend RPM.

For all other fabrics, libfabric needs to be installed. The host fabric stacks
for HPE Slingshot and Cornelis Networks Omni-Path ship with a version of
libfabric that has been validated with those fabrics.
In case the fabric host stack does not provide its own libfabric,
the DAOS packages directory does include an RPM build of libfabric
that can be used, for example when running on generic Ethernet interfaces.
The following section contains details on the versions of libfabric that
are used with the various fabric providers.

Details on the exact levels of the fabric host software stacks
that have been validated with DAOS can be found in the
[DAOS 2.8 Support Matrix](https://docs.daos.io/v2.8/release/support_matrix/),
which also provides more information and references
for the supported high performance fabrics.

## NIC firmware update

The adapter firmware on the Network Interface Cards (NICs) of the high speed
fabric is a critical component of any parallel storage system,
and DAOS is no exception.
Backlevel firmware or inconsistent firmware levels across nodes in the
fabric can cause operational issues that are hard to debug and resolve.
When installing the host software stack for the fabric, adapter firmware
levels should always be checked and updated to the correct level.
Refer to the respective fabric's documentation for details.

## Multiple NICs per host

Both DAOS server and DAOC clients may have more than one high-speed NIC,
with IP addresses in the same network range.
Some `sysctl` configuration is needed to ensure proper operation in such
scenarios, in particular around ARP resolution.  This is discussed in the
[Predeployment checklist](https://docs.daos.io/master/admin/predeployment_check/#multi-railnic-setup).

## Provider selection and configuration

On DAOS _servers_, the fabric provider is configured in the `daos_server.yml`
configuration file. Details for each supported fabric are given below.

The `daos_server network scan` command displays all network interfaces that
are recognized on a DAOS server for the provider that is set in the
`daos_server.yml` configuration file:

```
# daos_server network scan
DAOS Server config loaded from /etc/daos/daos_server.yml
---------
localhost
---------

    -------------
    NUMA Socket 0
    -------------

        Provider Interfaces
        -------- ----------
        ucx+dc_x ibs1

    -------------
    NUMA Socket 1
    -------------

        Provider Interfaces
        -------- ----------
        ucx+dc_x ibP1s3
```

To list **all** providers that are recognized for each of the server's
network interfaces, add the `--ignore-config` option. If an expected provider
is missing in the `daos_server network scan --ignore-config` output,
check if all the host software stack components are installed:

```
# daos_server network scan --ignore-config
---------
localhost
---------

    -------------
    NUMA Socket 1
    -------------

        Provider     Interfaces
        --------     ----------
        ucx+rc       ibP1s3
        ucx+rc_v     ibP1s3
        ucx+ud       ibP1s3
        ucx+dc       ibP1s3
        ucx+dc_x     ibP1s3
        ucx+rc_mlx5  ibP1s3
        ucx+tcp      ibP1s3
        ucx+rc_x     ibP1s3
        ucx+ud_verbs ibP1s3
        ucx+ud_x     ibP1s3
        ucx+all      ibP1s3, ibP1s3
        ucx+dc_mlx5  ibP1s3
        ucx+rc_verbs ibP1s3
        ucx+ud_mlx5  ibP1s3
        ucx+ud_v     ibP1s3

    -------------
    NUMA Socket 0
    -------------

        Provider     Interfaces
        --------     ----------
        ucx+ud_mlx5  ibs1
        ucx+tcp      bond0, ibs1
        ucx+rc       ibs1
        ucx+rc_mlx5  ibs1
        ucx+rc_verbs ibs1
        ucx+rc_x     ibs1
        ucx+dc_mlx5  ibs1
        ucx+dc_x     ibs1
        ucx+ud_v     ibs1
        ucx+ud_verbs ibs1
        ucx+all      bond0, ibs1, ibs1
        ucx+dc       ibs1
        ucx+rc_v     ibs1
        ucx+ud_x     ibs1
        ucx+ud       ibs1
```

DAOS _clients_ do not require explicit fabric provider configuration.
They receive their provider configuration from the DAOS servers through the
`daos_agent`. This can be checked with `daos_agent dump-attachinfo`.

DAOS clients **do** need to have the correct Mercury backend and fabric
host stack software installed to be able to communicate with the servers.
The `daos-agent net-scan` command displays all fabric providers that are
recognized on the DAOS client for each of its network interfaces.
If an expected provider is missing in the `daos_agent net-scan` output,
check if all the host software stack components are installed:

```
# daos_agent net-scan
---------
localhost
---------

    -------------
    NUMA Socket 0
    -------------

        Provider     Interfaces
        --------     ----------
        ucx+rc_mlx5  ibs1
        ucx+ud_v     ibs1
        ucx+dc       ibs1
        ucx+dc_x     ibs1
        ucx+rc_x     ibs1
        ucx+ud_verbs ibs1
        ucx+ud_x     ibs1
        ucx+rc       ibs1
        ucx+rc_v     ibs1
        ucx+rc_verbs ibs1
        ucx+ud       ibs1
        ucx+ud_mlx5  ibs1
        ucx+tcp      bond0, ibs1
        ucx+all      bond0, ibs1, ibs1
        ucx+dc_mlx5  ibs1
```

Unless they are also DAOS servers or DAOS clients, DAOS _admin nodes_
only use the control plane, so fabric provider configuration
does not apply to the admin node role.

### Generic TCP Fabrics with libfabric TCP

DAOS should run over any standard TCP/IP network, using the libfabric
`ofi+tcp` provider. This provider does **not** support RDMA transfers,
and the operating system's TCP stack is used which typically implies
higher CPU utilization, higher latency, and lower bandwidth than
 the other providers. The TCP provider should only be used on fabrics
where none of the other fabric providers can be used.

The required configuration in the `daos_server.yml` file is minimal:

```yaml
provider: ofi+tcp

engines:
-
  fabric_iface: eth4
-
  fabric_iface: eth5
```

To achieve good performance over TCP, the operating system's TCP/IP stack
has to be tuned for performance. To benefit from the performance of modern
fabrics like 400Gbps Ethernet, larger settings than the OS defaults are
often needed for many of the networking settings. In particular,
the TCP buffer sizes in `net.ipv4` (and the corresponding
settings in `net.core`) as well as other performance-related
TCP settings should be reviewed and tuned. The optimized settings need to be
made persistent, for example through a configuration file in `/etc/sysctl.d/`.

In addition to `sysctl` tuning, the **MTU size** of all network interfaces
in the fabric should be set to the largest supported value.
On native Ethernet fabrics, this is typically MTU=9000.
The MTU can be set in network interface configuration files like
`/etc/sysconfig/network-scripts/ifcfg-eth0`
or an equivalent `nmcli` configuration.

### RoCE on NVIDIA Ethernet with DOCA-OFED and UCX

While DAOS runs on any TCP network with the `ofi+tcp` provider,
DAOS also supports RDMA over Converged Ethernet (RoCE)
which will provide higher performance at lower CPU utilization.

In NVIDIA Ethernet environments, the recommended provider for
RoCE is `ucx+dc_x`
and a current DOCA-OFED level has to be installed on all hosts.
Note that while the `ofi+verbs` provider _should_ also support RoCE,
the verbs provider has scalability limitations and is not recommended.

Many NVIDIA network adapters like ConnectX-7 are
Virtual Protocol Interface (VPI) adapters: Each port can be set
to either InfiniBand mode (mode 1) or Ethernet mode (mode 2).
By default, all ports are set to InfiniBand. To change a port to
Ethernet, make sure that the `mst`  package is installed (it should
be part of the DOCA-OFED installation), and run the following steps
to determine the device name of the port to be changed,
change the link type, and reboot the node to make the change effective.
For example, with ConnectX-7 adapters (MT4129):

```bash
nmcli conn delete ib0
mst start
mst status # note the device type of the adapter to use
DEV="mt4129_pciconf0"
mlxconfig -d /dev/mst/$DEV query | grep LINK_TYPE
echo "y" | mlxconfig -d /dev/mst/$DEV set LINK_TYPE_P1=2

shutdown -r now

DEV="mt4129_pciconf0"
mlxconfig -d /dev/mst/$DEV query | grep LINK_TYPE
```

The recommendations in the "TCP" section regarding OS-level tuning of the
TCP stack (`sysctl` settings, MTU size) are also beneficial for a RoCE setup.
There will likely be other traffic on the high-speed Ethernet interfaces
that will benefit from this tuning, even if the RoCE transport itself does
not need this.

Example of the network-related configuration settings for RoCE
in the `daos_server.yml` file:

```yaml
provider: ucx+dc_x

engines:
-
  fabric_iface: ens1np0

  env_vars:
  - UCX_SOCKADDR_TLS_PRIORITY=rdmacm
  - UCX_IB_FORK_INIT=n
-
  fabric_iface: enP1s3np0

  env_vars:
  - UCX_SOCKADDR_TLS_PRIORITY=rdmacm
  - UCX_IB_FORK_INIT=n
```

Depending on the size of the fabric, other settings like the CaRT timeout
(set with `crt_timeout:` in the global section of the `daos_server.yml` file)
and/or the SWIM timeout settings (set as `SWIM_*` environment variables
within the `env_vars:` section of both engines) may also need to be adjusted.
But these settings depend on the cluster size and fabric details,
and there is no general recommendation to deviate from the defaults.

### NVIDIA InfiniBand with DOCA-OFED and UCX

The recommended provider for InfiniBand fabrics is `ucx+dc_x`,
and a current DOCA-OFED level has to be installed on all hosts.

Example of the network-related configuration settings for InfiniBand
in the `daos_server.yml` file:

```yaml
provider: ucx+dc_x

engines:
-
  fabric_iface: ibs1

  env_vars:
  - UCX_SOCKADDR_TLS_PRIORITY=rdmacm
  - UCX_IB_FORK_INIT=n
-
  fabric_iface: ibP1s3

  env_vars:
  - UCX_SOCKADDR_TLS_PRIORITY=rdmacm
  - UCX_IB_FORK_INIT=n
```

### HPE Slingshot with libfabric CXI

The recommended provider for Slingshot fabrics is `ofi+cxi`,
and a current level of the
[HPE Slingshot Host Software (SHS) stack](https://support.hpe.com/km/search#tab=All&q=slingshot%2014.0.1)
has to be installed on all hosts.

The SHS stack includes its own libfabric version,
which gets installed into `/opt/cray/libfabric/<version>/lib64/`.
For DAOS engines and DAOS clients to use the SHS version of libfabric,
the `LD_LIBRARY_PATH` needs to be set to point to this path.
On the DAOS servers, this is done in the `env_vars` section of the engines.

The following example of the network-related configuration settings for Slingshot
in `daos_server.yml` also includes several libfabric and Mercury tunables.
While the detailed settings depend on the size of the system,
this is a good starting point for Slingshot environments.

```yaml
provider: ofi+cxi

engines:
-
  fabric_iface: hsn0

  env_vars:
  - D_MRECV_BUF=16
  - D_MRECV_BUF_COPY=4
  - FI_CXI_OFLOW_BUF_SIZE=8388608
  - FI_CXI_OPTIMIZED_MRS=0
  - FI_CXI_RDZV_THRESHOLD=20480
  - FI_CXI_REQ_BUF_MIN_POSTED=8
  - FI_CXI_REQ_BUF_SIZE=8388608
  - FI_CXI_RX_MATCH_MODE=hybrid
  - FI_MR_CACHE_MONITOR=disabled
  - FI_CXI_DEFAULT_CQ_SIZE=131072
  - LD_LIBRARY_PATH=/opt/cray/libfabric/2.3.1/lib64
  - NA_OFI_SKIP_DOMAIN_OPS=1
  - SWIM_TRAFFIC_CLASS=low_latency
-
  fabric_iface: hsn1

  env_vars:
  - D_MRECV_BUF=16
  - D_MRECV_BUF_COPY=4
  - FI_CXI_OFLOW_BUF_SIZE=8388608
  - FI_CXI_OPTIMIZED_MRS=0
  - FI_CXI_RDZV_THRESHOLD=20480
  - FI_CXI_REQ_BUF_MIN_POSTED=8
  - FI_CXI_REQ_BUF_SIZE=8388608
  - FI_CXI_RX_MATCH_MODE=hybrid
  - FI_MR_CACHE_MONITOR=disabled
  - FI_CXI_DEFAULT_CQ_SIZE=131072
  - LD_LIBRARY_PATH=/opt/cray/libfabric/2.3.1/lib64
  - NA_OFI_SKIP_DOMAIN_OPS=1
  - SWIM_TRAFFIC_CLASS=low_latency
```

The cache monitor for the memory registration (MR) cache for RDMA transfers
is one of the key settings for Slingshot.
Refer to the "kdreg2" sections in the HPE Slingshot
[Host Software Installation and Configuration Guide](https://support.hpe.com/hpesc/public/docDisplay?docId=dp00008086en_us&page=install/kdreg2_introduction.html)
and
[Host Software Administration Guide](https://support.hpe.com/hpesc/public/docDisplay?docId=dp00008089en_us&page=operations/kdreg2_configuration.html).

On the DAOS servers, the recommendation is to disable `FI_MR_CACHE_MONITOR`.
On the DAOS clients, the recommendation is to use the `kdreg2` cache monitor.
The following tunables should be set in the user environment
on all DAOS client nodes
(for example through /etc/environment, /etc/profile.env, or a systemd service).
See the `DAOS` section in the
[Host Software User Guide)](https://support.hpe.com/hpesc/public/docDisplay?docId=dp00008090en_us&page=user/daos.html):

```
LD_LIBRARY_PATH=/opt/cray/libfabric/2.3.1/lib64
FI_CXI_RX_MATCH_MODE=hybrid
FI_MR_CACHE_MONITOR=kdreg2
FI_CXI_DEFAULT_CQ_SIZE=131072
```

It may also be beneficial to adjust the
`FI_MR_CACHE_MAX_SIZE` and `FI_MR_CACHE_MAX_COUNT`
tunables.

General TCP and Ethernet tuning recommanedations for Slingshot can be found
in the _TCP performance tuning_ and _Ethernet tuning_ sections of the
[HPE Slingshot Host Software Administration Guide](https://support.hpe.com/hpesc/public/docDisplay?docId=dp00008089en_us&page=performance/slingshot-eth-tuning.html).
The tuning script referenced therein is located in
`/opt/slingshot/utils/<version>/bin/slingshot-eth-tuning`.

### Cornelis Omni-Path with libfabric TCP or VERBS

For Omni-Path fabrics, the libfabric `psm2` and `opx` providers that are available for
MPI message-passing applicationos cannot be used with DAOS due to some functional gaps.

The libfabric `tcp` provider is supported on Omni-Path.
As with all high-speed fabrics, the MTU size is important to achieve good performance
with TCP. The default MTU size on Omni-Path is 2048, but it can be increased to 10240
by editing the configuration file of the Omni-Path fabric manager `/etc/opa-fm/opafm.xml`
and restarting the fabric manager (`systemctl restart opafm`). In the default
configuration, there is a single MulticastGroup and its MTU needs to be changed
to 10240. If multiple MulticastGroups exist change the MTU for all of them:

```xml
<MulticastGroup>
  <Create>1</Create>
  <MTU>10240</MTU>
  <Rate>100g</Rate>
  <!-- <SL>0</SL> -->
  <QKey>0x0</QKey>
  <TClass>0x0</TClass>
</MulticastGroup>
```

After restarting the fabric manager, hosts will automatically pick up the
increased MTU size, for example the `ip a show` command will
show an MTU size of 10236 (10240 minus a 4-byte header):

```
# ip a show ibs3d1 | head -1
9: ibs3d1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 10236 qdisc mq state UP group default qlen 1000
```

This MTU change will enable line rate performance on Omni-Path 100 fabrics.
But it will not be possible to saturate the 400Gbps link bandwidth of
Omni-Path CN5000 fabrics with TCP, even with the increased MTU size.

Cornelis has implemented a verbs API over CN5000, and the `ofi+verbs`
provider is the recommended provider for DAOS on CN5000.
To achieve the best performance, it is recommended to enable the Cornelis
_HFI service_ (previously called _Bulk Transfer Service (BTS)_)
in the Omni-Path SuperNIC driver:

```
cat /sys/module/hfi1/parameters/use_bulksvc
echo "options hfi1 use_bulksvc=Y" >> /etc/modprobe.d/hfi1.conf

dracut –f
reboot

cat /sys/module/hfi1/parameters/use_bulksvc
```

Before enabling the HFI service, querying the `use_bulksvc` driver parameter
will return `N`. When the service is enabled, it should return `Y`.
This needs to be done on each host in the CN5000 fabric.

Example of the network-related configuration settings for Omni-Path
CN5000 with the `ofi+verbs` API in the `daos_server.yml` file:

```yaml
provider: ofi+verbs

engines:
-
  fabric_iface: ibs3d1
-
  fabric_iface: ibs6d1
```

Depending on the size of the fabric, other settings like the CaRT timeout
(set with `crt_timeout:` in the global section of the `daos_server.yml` file)
and/or the SWIM timeout settings (set as `SWIM_*` environment variables
within the `env_vars:` section of both engines) may also need to be adjusted.
But these settings depend on the cluster size and fabric details,
and there is no general recommendation to deviate from the defaults.

