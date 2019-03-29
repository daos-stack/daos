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

* If the trigger environment variable is set in the user's shell, the control plane will use the environment variables set in the shell.
The config file will be ignored.

* If the trigger environment variable is NOT set in the user's shell, the shell environment variables will be overridden by the parameters set in the config file.

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

## NVMe management capabilities

Operations on NVMe SSD devices are performed using [go-spdk bindings](./go-spdk/README.md) to issue commands over the SPDK framework.

### NVMe Controller and Namespace Discovery

The following animation illustrates starting the control server and using the management shell to view the NVMe Namespaces discovered on a locally available NVMe Controller (assuming the quickstart_guide instructions have already been performed):

![Demo: List NVMe Controllers and Namespaces](/doc/graph/daosshellnamespaces.svg)

### NVMe Controller Firmware Update

The following animation illustrates starting the control server and using the management shell to update the firmware on a locally available NVMe Controller (assuming the quickstart_guide instructions have already been performed):

![Demo: Updating NVMe Controller Firmware](/doc/graph/daosshellfwupdate.svg)

### NVMe Controller Burn-in Validation

Burn-in validation is performed using the [fio tool](https://github.com/axboe/fio) which executes workloads over the SPDK framework using the [fio_plugin](https://github.com/spdk/spdk/tree/v18.04.1/examples/nvme/fio_plugin).

## SCM management capabilities

### SCM Module Discovery

### SCM Module Firmware Update

### SCM Module Burn-in Validation

Go app which can either be invoked with a subcommand to perform a specific task or without a subcommand which will launch an interactive shell.
Command-line subcommands are implemented with the [go-flags](https://github.com/jessevdk/go-flags) package and interactive shell with [ishell](https://github.com/abiosoft/ishell).

The management tool uses the [client API](../client) to interact with many [server](../server) instances as a gRPC client.
The management tool has no storage library dependencies and as such is suitable to be run from a login node to interact with storage nodes.

<details>
<summary>Usage info from app help</summary>
<p>

```
[tanabarr@ssh-1 ~]$ projects/daos_m/install/bin/daos_shell --help
Usage:
  daos_shell [OPTIONS] [command]

Application Options:
  -l, --hostlist=    comma separated list of addresses <ipv4addr/hostname:port> (default: localhost:10001)
  -f, --hostfile=    path of hostfile specifying list of addresses <ipv4addr/hostname:port>, if specified takes preference over HostList
  -o, --config-path= Client config file path

Help Options:
  -h, --help         Show this help message

Available commands:
  network  Perform tasks related to locally-attached network devices (aliases: n)
  pool     Perform tasks related to DAOS pools (aliases: p)
  service  Perform distributed tasks related to DAOS system (aliases: sv)
  storage  Perform tasks related to locally-attached storage (aliases: st)

[tanabarr@ssh-1 ~]$ projects/daos_m/install/bin/daos_shell storage --help
Usage:
  daos_shell [OPTIONS] storage <list>

Application Options:
  -l, --hostlist=    comma separated list of addresses <ipv4addr/hostname:port> (default: localhost:10001)
  -f, --hostfile=    path of hostfile specifying list of addresses <ipv4addr/hostname:port>, if specified takes preference over HostList
  -o, --config-path= Client config file path

Help Options:
  -h, --help         Show this help message

Available commands:
  list  List locally-attached SCM and NVMe storage (aliases: l)

[tanabarr@ssh-1 ~]$ projects/daos_m/install/bin/daos_shell service --help
Usage:
  daos_shell [OPTIONS] service <kill-rank>

Application Options:
  -l, --hostlist=    comma separated list of addresses <ipv4addr/hostname:port> (default: localhost:10001)
  -f, --hostfile=    path of hostfile specifying list of addresses <ipv4addr/hostname:port>, if specified takes preference over HostList
  -o, --config-path= Client config file path

Help Options:
  -h, --help         Show this help message

Available commands:
  kill-rank  Terminate server running as specific rank on a DAOS pool (aliases: kr)

[tanabarr@ssh-1 ~]$ projects/daos_m/install/bin/daos_shell service kill-rank --help
Usage:
  daos_shell [OPTIONS] service kill-rank [kill-rank-OPTIONS]

Application Options:
  -l, --hostlist=      comma separated list of addresses <ipv4addr/hostname:port> (default: localhost:10001)
  -f, --hostfile=      path of hostfile specifying list of addresses <ipv4addr/hostname:port>, if specified takes preference over HostList
  -o, --config-path=   Client config file path

Help Options:
  -h, --help           Show this help message

[kill-rank command options]
      -r, --rank=      Rank identifying DAOS server
      -p, --pool-uuid= Pool uuid that rank relates to
```

</p>
</details>

<details>
<summary>Example output from invoking "storage list" subcommand</summary>
<p>

```
[tanabarr@ssh-1 ~]$ projects/daos_m/install/bin/daos_shell -l boro-44:10001,boro-45:10001 storage list
Active connections: [boro-45:10001 boro-44:10001]

Listing NVMe SSD controller and constituent namespaces on connected storage servers:
boro-44:10001:
- id: 0
  model: 'INTEL SSDPED1K375GA '
  serial: 'PHKS73350016375AGN  '
  pciaddr: 0000:81:00.0
  fwrev: E2010324
  namespace:
  - id: 1
    capacity: 375
boro-45:10001:
- id: 0
  model: 'INTEL SSDPED1K375GA '
  serial: 'PHKS7335006W375AGN  '
  pciaddr: 0000:81:00.0
  fwrev: E2010420
  namespace:
  - id: 1
    capacity: 375


Listing SCM modules on connected storage servers:
boro-44:10001: []
boro-45:10001: []
```

</p>
</details>

<details>
<summary>Example output when listing storage in interactive mode</summary>
<p>

```
[tanabarr@ssh-1 ~]$ projects/daos_m/install/bin/daos_shell
Active connections: [localhost:10001]

DAOS Management Shell
>>> help

Commands:
  addconns          Command to create connections to servers by supplying a space separated list of addresses <ipv4addr/hostname:port>
  clear             clear the screen
  clearconns        Command to clear stored server connections
  exit              exit the program
  getconns          Command to list active server connections
  help              display help
  killrank          Command to terminate server running as specific rank on a DAOS pool
  listfeatures      Command to retrieve supported management features on connected servers
  liststorage       Command to list locally-attached NVMe SSD controllers and SCM modules


>>> addconns boro-44:10001 boro-45:10001
failed to connect to localhost:10001 (socket connection is not active (TRANSIENT_FAILURE))
Active connections: [boro-45:10001 boro-44:10001]

>>> liststorage
Active connections: [boro-45:10001 boro-44:10001]

Listing NVMe SSD controller and constituent namespaces on connected storage servers:
boro-44:10001:
- id: 0
  model: 'INTEL SSDPED1K375GA '
  serial: 'PHKS73350016375AGN  '
  pciaddr: 0000:81:00.0
  fwrev: E2010324
  namespace:
  - id: 1
    capacity: 375
boro-45:10001:
- id: 0
  model: 'INTEL SSDPED1K375GA '
  serial: 'PHKS7335006W375AGN  '
  pciaddr: 0000:81:00.0
  fwrev: E2010420
  namespace:
  - id: 1
    capacity: 375


Listing SCM modules on connected storage servers:
boro-44:10001: []
boro-45:10001: []
```

</p>
</details>
