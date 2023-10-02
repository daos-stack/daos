# VMD Support in DAOS

[Intel VMD (Volume Management Device)](https://www.intel.com/content/www/us/en/architecture-and-technology/intel-volume-management-device-overview.html)
is a feature introduced with the
Intel Xeon Scalable processor family to help manage NVMe drives.
It provides features such as **surprise hot plug, LED management, 
error isolation** and **bootable RAID**.

The Intel VMD functionality is provided as part of the
[Intel VROC (Virtual RAID on CPU)](https://www.intel.com/content/www/us/en/architecture-and-technology/intel-volume-management-device-overview.html)
technology, and resides within the Intel Xeon CPUs.
If RAID is not needed, then Intel VROC can be used in pass-through
mode to turn on Intel VMD Domains only.

!!! note DAOS is not using the VROC RAID functionality.
         The DAOS erasure coding functionality already
         provides data protection across servers, so there is no benefit from
         providing VROC RAID functionality within a single DAOS server.

Starting with DAOS 2.2, DAOS can _optionally_ use NVMe devices that are members
of VMD domains. In order to use VMD, this functionality first has to be enabled
in the servers' UEFI. It can then be also enabled in the `daos_server.yml`
configuration file, as described below.

DAOS 2.2 did enable VMD-managed devices in the `daos_server.yml` configuration file
(and as arguments to some DAOS management commands), but did 
_not_ yet provide any additional functionality over non-VMD devices.

DAOS 2.4 introduced the **LED management** feature that requires VMD.

**Surprise hot-plug management** through VMD is roadmap item for DAOS 2.6.

This document explains how to enable VMD.
Customers who intend to utilize DAOS capabilities that depend on VMD
are encouraged to enable VMD when setting up the DAOS cluster, because changing from a
non-VMD setup to VMD is not possible without reformatting the DAOS storage.


## NVMe view with VMD disabled (before binding to SPDK)

The following is an example of the `lspci` view on a server with eight
NVMe SSDs, when VMD is _disabled_. This is the status when the devices are
still bound to the kernel (before running `daos_server storage prepare –n`):

```bash
[root@nvm0806 ~]# lspci -vv | grep -i nvme
65:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
66:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
67:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
68:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
e3:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
e4:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
e5:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
e6:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
```

After running `daos_server storage prepare -n`, the NVMe SSDs are bound
to SPDK, and `lspci` or `nvme list` no longer show them.


## Enabling VMD in the server UEFI

Before DAOS can use VMD devices, VMD needs to be enabled in the servers' UEFI.
Details depend on the server vendor.
An example for this setting is `DevicesandIOPorts.EnableDisableIntelVMD=Enabled`.

After enabling VMD in UEFI, the servers need to be rebooted to activate it.


## NVMe view with VMD enabled (before binding to SPDK)

When VMD is correctly _enabled_ in UEFI, after the reboot the `lspci` output
should show new VMD controller devices in addition to the NVMe SSDs themselves.
Note that the PCIe addresses of the VMD-managed NVMe SSDs are different from
the non-VMD case, highlighting that they are now backing devices behind a VMD
controller device.

```bash
[root@nvm0806 ~]# lspci -vv | grep -i nvme
0000:64:00.5 RAID bus controller: Intel Corporation Volume Management Device NVMe RAID Controller (rev 04)
0000:c9:00.5 RAID bus controller: Intel Corporation Volume Management Device NVMe RAID Controller (rev 04)
0000:e2:00.5 RAID bus controller: Intel Corporation Volume Management Device NVMe RAID Controller (rev 04)
10000:81:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
10000:82:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
10000:83:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
10000:84:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
10001:01:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
10001:02:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
10002:01:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
10002:02:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
10002:03:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
10002:04:00.0 Non-Volatile memory controller: Intel Corporation NVMe DC SSD [3DNAND, Sentinel Rock Controller] (prog-if 02 [NVM Express])
```

In this particular example, the eight NVMe disks belong to two different VMD domains
(each VMD domain comprises 16 PCIe lanes, and each NVMe SSD uses 4 lanes).
The `lspci` output also shows a third VMD controller, which does not have
any NVMe backing devices. This is due to the fact that this server has
additional NVMe drive slots, but those slots are not populated with NVMe SSDs.


## NVMe view with VMD enabled (after binding to SPDK)

After `daos_server storage prepare -n` has been run on a VMD-enabled DAOS server,
the NVMe disks are unbound from the Linux kernel and no longer show up in `lspci` or
`nvme list` (just like in the non-VMD case).
However, the VMD controller devices are still visible with `lspci`:

```bash
[root@nvm0806 ~]# lspci | grep -i nvme
0000:64:00.5 RAID bus controller: Intel Corporation Volume Management Device NVMe RAID Controller (rev 07)
0000:e2:00.5 RAID bus controller: Intel Corporation Volume Management Device NVMe RAID Controller (rev 07)
0000:c9:00.5 RAID bus controller: Intel Corporation Volume Management Device NVMe RAID Controller (rev 07)
```

The VMD-managed NVMe backing devices now show up in the DAOS storage scan, with their VMD IDs:

```bash
[root@nvm0806 ~]# daos_server storage scan
Scanning locally-attached storage...
NVMe PCI       Model          FW Revision Socket ID Capacity
--------       -----          ----------- --------- --------
640005:81:00.0 SSDPF2KX038T9L 2CV1L028    0         3.8 TB
640005:83:00.0 SSDPF2KX038T9L 2CV1L028    0         3.8 TB
640005:85:00.0 SSDPF2KX038T9L 2CV1L028    0         3.8 TB
640005:87:00.0 SSDPF2KX038T9L 2CV1L028    0         3.8 TB
e20005:01:00.0 SSDPF2KX038T9L 2CV1L028    1         3.8 TB
e20005:03:00.0 SSDPF2KX038T9L 2CV1L028    1         3.8 TB
e20005:05:00.0 SSDPF2KX038T9L 2CV1L028    1         3.8 TB
e20005:07:00.0 SSDPF2KX038T9L 2CV1L028    1         3.8 TB
```


## Using VMD Devices in the DAOS server configuration file

The recommended setup to use VMD devices within the DAOS server configuration file
is to list the PCIe IDs of the _VMD controllers_ in the storage engines' `bdev_list`.
This ensures that all NVMe disks that are members of a VMD domain are always
managed together, and assigned to the same DAOS storage engine.
The `daos_server.yml` file for the above example would have one VMD domain per engine:

```yaml
storage: # engine 0
  -
    class: dcpm
    scm_mount: /var/daos/pmem0
    scm_list:
    - /dev/pmem0
  -
    class: nvme
    bdev_list:
    - "0000:64:00.5"

storage: # engine 1
  -
    class: dcpm
    scm_mount: /var/daos/pmem1
    scm_list:
    - /dev/pmem1
  -
    class: nvme
    bdev_list:
    - "0000:e2:00.5"
```
