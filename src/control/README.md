# DAOS Control Plane (daos_server)

DAOS operates over two, closely integrated planes, Control and Data. The Data plane handles the heavy lifting transport operations while the Control plane orchestrates process and storage management, facilitating the operation of the Data plane.

[DAOS Server](server/daos_server.go) implements the DAOS Control Plane and is written in Golang. It is tasked with network and storage hardware provisioning and allocation in addition to instantiation and management of the DAOS IO Servers (Data Plane written in C) running on the same host. Users of DAOS will interact directly only with the Control Plane in the form of the DAOS Server and associated tools.

The DAOS Server implements the [gRPC protocol](https://grpc.io/) to communicate with client gRPC applications and interacts with DAOS IO Servers through Unix domain sockets.

Multiple gRPC server modules are loaded by the control server. Currently included modules are security and management.

The Control Plane implements a replicated management service as part of the DAOS Server, responsible for handling distributed operations across the DAOS System.

The [shell](dmg/daos_shell) is an example client application which can connect to both the [agent](agent/daos_agent.go) to perform security functions (such as providing credentials and retrieving security contexts) and to the local management server to perform management functions (such as storage device discovery).

## Documentation

- [Management API](https://godoc.org/github.com/daos-stack/daos/src/control/client)
- [Management internals](https://godoc.org/github.com/daos-stack/daos/src/control/server)
- [Agent API](https://godoc.org/github.com/daos-stack/daos/src/control/client/agent)
- [Agent internals](https://godoc.org/github.com/daos-stack/daos/src/control/security)
- [dRPC](https://godoc.org/github.com/daos-stack/daos/src/control/drpc)

## Configuration

`daos_server` config file is parsed when starting `daos_server` process, it's location can be specified on the commandline (`-o` option) or default location (`<daos install dir>/install/etc/daos_server.yml`).

Example config files can be found in the [examples folder](https://github.com/daos-stack/daos/tree/master/utils/config/examples).

Some parameters will be parsed and populated with defaults as documented in the [default daos server config](https://github.com/daos-stack/daos/tree/master/utils/config/daos_server.yml) if not present in config file.

Parameters passed to `daos_server` on the commandline as application options (excluding environment variables) take precedence over values specified in config file.

For convenience, active parsed config values are written to the directory where the server config file was read from or `/tmp/` if that fails.

If user shell executing `daos_server` has environment variable `CRT_PHY_ADDR_STR` set, user os environment will be used when spawning `daos_io_server` instances. In this situation an error message beginning "using os env vars..." message will be printed and no environment variables will be added as specified in the `env_vars` list within the per-server section of the server config file. This behaviour provides backward compatibility with historic mechanism of specifying all parameters through environment variables.

It is strongly recommended to specify all parameters and environment for running DAOS servers in the [server config file](https://github.com/daos-stack/daos/tree/master/utils/config/daos_server.yml).

To clarify:

* If the trigger environment variable is set in the users shell, the control plane will not set the values in the config file and environment variables in shell will be used.

* If the trigger environment variable is not set in the users shell, the control plane will set the values in the config file in preference to those set in the users shell (the users shell environment variables will be overridden by the parameters set in the config file)

## Subcommands

`daos_server` supports various subcommands (see `daos_server --help` for available subcommands) which will perform stand-alone tasks as opposed to launching as a daemon (default operation if launched without subcommand).

### prep-nvme

This subcommand requires elevated permissions and needs to be run with root permissions (sudo).

NVMe access through SPDK as an unprivileged user can be enabled by first running `sudo daos_server prep-nvme -p 4096 -u bob`. This will perform the required setup in order for `daos_server` to be run by user "bob" who will own the hugepage mountpoint directory and vfio groups as needed in SPDK operations. If the `target-user` is unspecified (`-u` short option), the target user will be the issuer of the sudo command (or root if not using sudo). The specification of `hugepages` (`-u` short option) defines the number of huge pages to allocate for use by SPDK.

The configuration commands that require elevated permissions are in `src/control/mgmt/init/setup_spdk.sh` (script is installed as `install/share/setup_spdk.sh`).

The sudoers file can be accessed with command `visudo` and permissions can be granted to a user to execute a specific command pattern (requires prior knowledge of `daos_server` binary location):
```
linuxuser ALL=/home/linuxuser/projects/daos_m/install/bin/daos_server prep-nvme*
```

See `daos_server prep-nvme --help` for usage.

### show-storage

List NVMe SSDs and SCM modules locally attached to the host.

See `daos_server show-storage --help` for usage.

## Shell Usage

In order to run the shell to perform administrative tasks, build and run the `daos_server` as per the [quickstart guide](https://github.com/daos-stack/daos/blob/master/doc/quickstart.md).

`daos_server` is to be run as root in order to perform administrative tasks, to be run through `orterun` as root:

```
root$ orterun -np 1 -c 1 --hostfile hostfile --enable-recovery --allow-run-as-root --report-uri /tmp/urifile daos_server -c 1
```

`daos_shell` (the management tool to exercise the client api) is to be run on login nodes by an unprivileged user (and is designed to be lightweight without dependencies on storage libraries).  The shell can be used to connect to and interact with multiple gRPC servers concurrently (running on port 10000 by default) as follows:
```
$ projects/daos_m/install/bin/daos_shell -l foo-45:10001,foo-44:10001
DAOS Management Shell
>>>
```

See `daos_shell --help` for usage.

## NVMe management capabilities

Operations on NVMe SSD devices are performed using [go-spdk bindings](./go-spdk/README.md) to issue commands over the SPDK framework.

### NVMe Controller and Namespace Discovery

The following animation illustrates starting the control server and using the management shell to view the NVMe Namespaces discovered on a locally available NVMe Controller (assuming the quickstart_guide instructions have already been performed):

![Demo: List NVMe Controllers and Namespaces](./media/daosshellnamespaces.svg)

### NVMe Controller Firmware Update

The following animation illustrates starting the control server and using the management shell to update the firmware on a locally available NVMe Controller (assuming the quickstart_guide instructions have already been performed):

![Demo: Updating NVMe Controller Firmware](./media/daosshellfwupdate.svg)

### NVMe Controller Burn-in Validation

Burn-in validation is performed using the [fio tool](https://github.com/axboe/fio) which executes workloads over the SPDK framework using the [fio_plugin](https://github.com/spdk/spdk/tree/v18.04.1/examples/nvme/fio_plugin).

## SCM management capabilities

### SCM Module Discovery

### SCM Module Firmware Update

### SCM Module Burn-in Validation

## Architecture

First a view of software component architecture:

![Architecture diagram](./media/control_architecture.PNG)

Then communication interfaces:

```
    ┌───────────────┐ ┌───────────────┐
    │  Go Shell     │ │ Other Client  │
    └───────────────┘ └───────────────┘
            │                 │
            └────────┬────────┘
                     ▼
          ┌─────────────────────┐
          │    Go daos_server   │----|
          └─────────────────────┘    |
                     │               |
                     ▼               |
       ┌───────────────────────────┐ |
       │     Unix Domain Socket    │ |
       └───────────────────────────┘ |
                     │               |
                     ▼               |
          ┌─────────────────────┐    |
          │   C daos_io_server  │    |
          └─────────────────────┘    |
                     │               |
                     ▼               |
           ┌────────────────────┐    |
           │ Persistent Storage │<---|
           └────────────────────┘
```
TODO: include details of `daos_agent` interaction

## Development Requirements

* [Golang](https://golang.org/) 1.9 or higher
* [gRPC](https://grpc.io/)
* [Protocol Buffers](https://developers.google.com/protocol-buffers/)
* [Dep](https://github.com/golang/dep/) for managing dependencies in vendor directory.

## Development setup

* If changing vendor package versions, edit `src/control/Gopkg.toml` and then run `dep ensure` from src/control.
* (Optional) protoc protocol buffer compiler

### Building the app

#### Local

* `scons` (binaries should be produced in `install/bin` directory)

### Testing the app

* Run the tests `go test` within each directory containing tests

## Coding Guidelines

### daos_server and daos_agent

* Avoid calling `os.Exit` (or `log.Fatal`, `log.Fatalf`, etc.), except for assertion purposes. Fatal errors shall be returned back to `main`, who determines the exit status based on its `err` and calls `os.Exit`, in its last deferred function call.
