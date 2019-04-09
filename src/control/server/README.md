# DAOS Server (control-plane)

## Running

`daos_server` binary should be run as an MPI app using a distributed launcher such as `orterun`.

For instructions on building and running DAOS see the [Quickstart guide](../../doc/quickstart.md).

<details>
<summary>Example output from invoking `daos_server` on multiple hosts with `orterun` (with logging to stdout for illustration purposes)</summary>
<p>

```
[tanabarr@boro-45 daos_m]$ orterun -np 2 -H boro-44,boro-45 --report-uri /tmp/urifile --enable-recovery daos_server -c 1  -o /home/tanabarr/projects/daos_m/utils/config/examples/daos_server_sockets.yml
2019/03/28 12:28:07 config.go:85: debug: DAOS config read from /home/tanabarr/projects/daos_m/utils/config/examples/daos_server_sockets.yml
2019/03/28 12:28:07 config.go:85: debug: DAOS config read from /home/tanabarr/projects/daos_m/utils/config/examples/daos_server_sockets.yml
2019/03/28 12:28:07 main.go:79: debug: Switching control log level to DEBUG
boro-44.boro.hpdd.intel.com 2019/03/28 12:28:07 config.go:121: debug: Active config saved to /home/tanabarr/projects/daos_m/utils/config/examples/.daos_server.active.yml (read-only)
Starting SPDK v18.07-pre / DPDK 18.02.0 initialization...
[ DPDK EAL parameters: spdk -c 0x1 --file-prefix=spdk234203216 --base-virtaddr=0x200000000000 --proc-type=auto ]
EAL: Detected 72 lcore(s)
EAL: Auto-detected process type: PRIMARY
2019/03/28 12:28:07 main.go:79: debug: Switching control log level to DEBUG
boro-45.boro.hpdd.intel.com 2019/03/28 12:28:07 config.go:121: debug: Active config saved to /home/tanabarr/projects/daos_m/utils/config/examples/.daos_server.active.yml (read-only)
EAL: Detected 72 lcore(s)
Starting SPDK v18.07-pre / DPDK 18.02.0 initialization...
[ DPDK EAL parameters: spdk -c 0x1 --file-prefix=spdk290246766 --base-virtaddr=0x200000000000 --proc-type=auto ]
EAL: Auto-detected process type: PRIMARY
EAL: No free hugepages reported in hugepages-1048576kB
EAL: No free hugepages reported in hugepages-1048576kB
EAL: Multi-process socket /home/tanabarr/.spdk234203216_unix
EAL: Probing VFIO support...
EAL: Multi-process socket /home/tanabarr/.spdk290246766_unix
EAL: Probing VFIO support...
EAL: PCI device 0000:81:00.0 on NUMA socket 1
EAL:   probe driver: 8086:2701 spdk_nvme
EAL: PCI device 0000:81:00.0 on NUMA socket 1
EAL:   probe driver: 8086:2701 spdk_nvme
Starting SPDK v18.07-pre / DPDK 18.02.0 initialization...
[ DPDK EAL parameters: daos -c 0x1 --file-prefix=spdk290246766 --base-virtaddr=0x200000000000 --proc-type=auto ]
EAL: Detected 72 lcore(s)
Starting SPDK v18.07-pre / DPDK 18.02.0 initialization...
[ DPDK EAL parameters: daos -c 0x1 --file-prefix=spdk234203216 --base-virtaddr=0x200000000000 --proc-type=auto ]
EAL: Auto-detected process type: SECONDARY
EAL: Detected 72 lcore(s)
EAL: Auto-detected process type: SECONDARY
EAL: Multi-process socket /home/tanabarr/.spdk234203216_unix_141938_a591f56066dd7
EAL: Probing VFIO support...
EAL: WARNING: Address Space Layout Randomization (ASLR) is enabled in the kernel.
EAL:    This may cause issues with mapping memory into secondary processes
EAL: Multi-process socket /home/tanabarr/.spdk290246766_unix_23680_14e5a164a3bd1db
EAL: Probing VFIO support...
EAL: WARNING: Address Space Layout Randomization (ASLR) is enabled in the kernel.
EAL:    This may cause issues with mapping memory into secondary processes
EAL: PCI device 0000:81:00.0 on NUMA socket 1
EAL:   probe driver: 8086:2701 spdk_nvme
boro-44.boro.hpdd.intel.com 2019/03/28 12:28:11 main.go:188: debug: DAOS server listening on 0.0.0.0:10001
DAOS I/O server (v0.0.2) process 141938 started on rank 1 (out of 2) with 1 target xstream set(s).
EAL: PCI device 0000:81:00.0 on NUMA socket 1
EAL:   probe driver: 8086:2701 spdk_nvme
boro-45.boro.hpdd.intel.com 2019/03/28 12:28:11 main.go:188: debug: DAOS server listening on 0.0.0.0:10001
DAOS I/O server (v0.0.2) process 23680 started on rank 0 (out of 2) with 1 target xstream set(s).
```

</p>
</details>

## Configuration

`daos_server` config file is parsed when starting `daos_server` process, it's location can be specified on the commandline (`-o` option) or default location (`<daos install dir>/install/etc/daos_server.yml`).

Example config files can be found in the [examples folder](https://github.com/daos-stack/daos/tree/master/utils/config/examples).

Some parameters will be parsed and populated with defaults as documented in the [default daos server config](https://github.com/daos-stack/daos/tree/master/utils/config/daos_server.yml) if not present in config file.

Parameters passed to `daos_server` on the commandline as application options (excluding environment variables) take precedence over values specified in config file.

For convenience, active parsed config values are written to the directory where the server config file was read from or `/tmp/` if that fails.

If user shell executing `daos_server` has environment variable `CRT_PHY_ADDR_STR` set, user os environment will be used when spawning `daos_io_server` instances. In this situation an error message beginning "using os env vars..." will be printed and no environment variables will be added as specified in the `env_vars` list within the per-server section of the server config file. This behaviour provides backward compatibility with historic mechanism of specifying all parameters through environment variables.

It is strongly recommended to specify all parameters and environment for running DAOS servers in the [server config file](https://github.com/daos-stack/daos/tree/master/utils/config/daos_server.yml).

To clarify with respect to environment variables affecting the behaviour of `daos_io_server` instances:

*  If the trigger environment variable is set in the user's shell, the control plane will use the environment variables set in the shell. The config file will be ignored.
*  If the trigger environment variable is NOT set in the user's shell, the shell environment variables will be overridden by the parameters set in the config file.

### Configuring logging through the config file

Log file and level mask for both data (`daos_io_server`) and control (`daos_server`) planes can be set in the server config file.
TODO: examples for global (control) and per-server (data) log parameters in config including DAOS specific syntax (D_LOG_MASK=ERR,mgmt=DEBUG).

### Selecting and formatting NVMe/block device storage through the config file

Parameters prefixed with `nvme_` in the per-server section of the config file determine how NVMe storage will be assigned for use by DAOS on the storage node.
TODO: examples for NVMe and AIO (emulation) classes including config file syntax and results/logging of formatting operations including generated nvme.conf file and SPDK device format through language bindings.

### Selecting and formatting SCM/pmem storage through the config file

Parameters prefixed with `scm_` in the per-server section of the config file determine how SCM storage will be assigned for use by DAOS on the storage node.
TODO: examples for both DCPM and RAM (emulation) SCM classes including config file syntax and results/logging of formatting operations.

## Subcommands

`daos_server` supports various subcommands (see `daos_server --help` for available subcommands) which will perform stand-alone tasks as opposed to launching as a daemon (default operation if launched without subcommand).

### storage prep-nvme

This subcommand requires elevated permissions (sudo).

NVMe access through SPDK as an unprivileged user can be enabled by first running `sudo daos_server prep-nvme -p 4096 -u bob`. This will perform the required setup in order for `daos_server` to be run by user "bob" who will own the hugepage mountpoint directory and vfio groups as needed in SPDK operations. If the `target-user` is unspecified (`-u` short option), the target user will be the issuer of the sudo command (or root if not using sudo). The specification of `hugepages` (`-p` short option) defines the number of huge pages to allocate for use by SPDK.

The configuration commands that require elevated permissions are in `src/control/mgmt/init/setup_spdk.sh` (script is installed as `install/share/setup_spdk.sh`).

The sudoers file can be accessed with command `visudo` and permissions can be granted to a user to execute a specific command pattern (requires prior knowledge of `daos_server` binary location):
```
linuxuser ALL=/home/linuxuser/projects/daos_m/install/bin/daos_server prep-nvme*
```

See `daos_server prep-nvme --help` for usage.

### storage list

<details>
<summary>List NVMe SSDs and SCM modules locally attached to the host.</summary>
<p>

```
[tanabarr@boro-45 daos_m]$ daos_server storage list
Starting SPDK v18.07-pre / DPDK 18.02.0 initialization...
[ DPDK EAL parameters: spdk -c 0x1 --file-prefix=spdk_pid29193 ]
EAL: Detected 72 lcore(s)
EAL: No free hugepages reported in hugepages-1048576kB
EAL: Multi-process socket /home/tanabarr/.spdk_pid29193_unix
EAL: Probing VFIO support...
Unable to unlink shared memory file: /var/run/.spdk_pid29193_config. Error code: 2
Unable to unlink shared memory file: /var/run/.spdk_pid29193_hugepage_info. Error code: 2
EAL: PCI device 0000:81:00.0 on NUMA socket 1
EAL:   probe driver: 8086:2701 spdk_nvme
Listing attached storage...
NVMe:
- id: 0
  model: 'INTEL SSDPED1K375GA '
  serial: 'PHKS7335006W375AGN  '
  pciaddr: 0000:81:00.0
  fwrev: E2010420
  namespace:
  - id: 1
    capacity: 375
```

</p>
</details>

See `daos_server show-storage --help` for usage.

## Management Tool (client) Usage

In order to run the tool to perform administrative tasks, build and run the `daos_server` as per the [quickstart guide](https://github.com/daos-stack/daos/blob/master/doc/quickstart.md).

[`daos_shell`](../dmg) is a management tool which exercises the client api and can be run on login nodes by an unprivileged user. The tool is lightweight and doesn't depend on storage libraries. The tool (a gRPC client application) connects and interacts with multiple gRPC servers concurrently, connecting to specified ports.

See `daos_shell --help` for usage and [here](../dmg) for package details.

## Storage management

The DAOS data plane utilises two forms of non-volatile storage, storage class memory (SCM) in the form of persistent memory modules and NVMe in the form of high-performance SSDs.

### SCM management capabilities

Operations on SCM persistent memory modules are performed using [go-ipmctl bindings](https://github.com/daos-stack/go-ipmctl) to issue commands through the ipmctl native C libraries.

Provisioning SCM occurs by configuring modules in AppDirect memory regions (interleaved mode) in groups of modules local to a specific socket (NUMA) and resultant nvdimm namespaces are defined a device identifier (e.g. /dev/pmem0).

Formatting SCM involves creating an ext4 filesystem on the nvdimm device.

Mounting SCM results in an active mount using the DAX extension enabling direct access without restrictions imposed by legacy HDD hardware.

#### SCM Module Discovery

Device details for any discovered (Intel) data-centre persistent memory modules (DCPM modules) on the storage server will be returned.

TODO: return details of AppDirect memory regions

#### SCM Format

Format can be triggered through the management tool at DAOS system installation time, formatting and mounting of SCM device namespace is will be performed as specified in config file parameters prefixed with `scm_`.

#### SCM Firmware Update

#### SCM Burn-in Validation

### NVMe management capabilities

Operations on NVMe SSD devices are performed using [go-spdk bindings](https://github.com/daos-stack/go-spdk) to issue commands over the SPDK framework.

The DAOS data plane utilises NVMe devices as block storage using the SPDK "[blobstore](https://spdk.io/doc/blob.html)" abstraction, creation and management of the blobstore and blobs is performed by the data plane.

#### NVMe Controller and Namespace Discovery

Device details for any discovered NVMe SSDs accessible through SPDK on the storage server will be returned.

The following animation illustrates starting the control server and using the management shell to view the NVMe Namespaces discovered on a locally available NVMe Controller (assuming the quickstart guide instructions have already been performed):

![Demo: List NVMe Controllers and Namespaces](/doc/graph/daosshellnamespaces.svg)

#### NVMe Format

Format can be triggered through the management tool at DAOS system installation time.

In the context of what is required from the control plane to prepare NVMe devices for operation with DAOS data plane, "formatting" refers to the low level reset of storage media which will remove blobstores and remove any filesystem signatures from the SSD controller namespaces.

Formatting will be performed on devices identified by PCI addresses specified in config file parameter `bdev_list` when `bdev_class` is equal to `nvme`.

The SPDK "[blobcli](https://github.com/spdk/spdk/tree/master/examples/blob/cli)" can be used to initiate and manipulate blobstores for testing and verification purposes.

#### NVMe Controller Firmware Update

The following animation illustrates starting the control server and using the management shell to update the firmware on a locally available NVMe Controller (assuming the quickstart guide instructions have already been performed):

![Demo: Updating NVMe Controller Firmware](/doc/graph/daosshellfwupdate.svg)

#### NVMe Controller Burn-in Validation

Burn-in validation is performed using the [fio tool](https://github.com/axboe/fio) which executes workloads over the SPDK framework using the [fio plugin](https://github.com/spdk/spdk/tree/v18.04.1/examples/nvme/fio_plugin).

