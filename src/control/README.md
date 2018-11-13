# daos_server

This Go package provides a [control server](server/daos_server.go) that implements the DAOS control-plane. The control server is a Go process responsible for the initialisation and management of the DAOS IO servers mainly through a Unix domain socket.

The control server implements the [gRPC protocol](https://grpc.io/) to communicate with client gRPC applications.

Multiple gRPC server modules are loaded by the control server. Currently included modules are security and management.

The [shell](dmg/daos_shell) is an example client application which can connect to both the [agent](agent/daos_agent.go) to perform security functions (such as providing credentials and retrieving security contexts) and to the local management server to perform management functions (such as storage device discovery).

## Documentation

- [Management API](https://godoc.org/github.com/daos-stack/daos/src/control/client/mgmt)
- [Management internals](https://godoc.org/github.com/daos-stack/daos/src/control/mgmt)
- [Agent API](https://godoc.org/github.com/daos-stack/daos/src/control/client/agent)
- [Agent internals](https://godoc.org/github.com/daos-stack/daos/src/control/security)

## Configuration

daos_server configuration file is parsed when starting daos_server process, it's location can be specified on the commandline (daos_server -h for usage) or default location (install/etc/daos_server.yml).

Parameters will be parsed and populated with defaults (hardcoded in source) if not present in configuration.

Commandline parameters take precedence over configuration file values but if not specified on commandline, configuration file values will be applied (or parsed defaults).

For convenience, active parsed config values are written to either directory where config file was read from or /tmp/ if that fails.

## Shell Usage

In order to run the shell to perform administrative tasks, build and run the `daos_server` as per the [quickstart guide](https://github.com/daos-stack/daos/blob/master/doc/quickstart.md).

`daos_server` is to be run as root in order to perform administrative tasks, to be run through `orterun` as root:

```
root$ orterun -np 1 -c 1 --hostfile hostfile --enable-recovery --allow-run-as-root --report-uri /tmp/urifile daos_server -c 1
```

daos_shell (the management tool to exercise the client api) is to be run as a standard, unprivileged user.  The shell can be used to connect to and interact with the gRPC server (running on port 10000 by default) as follows:
```
$ projects/daos_m/install/bin/daos_shell
DAOS Management Shell
>>> connect -t '127.0.0.1:10000'
>>> help
Commands:
  clear                 clear the screen
  connect               Connect to management infrastructure
  exit                  exit the program
  gethandle             Command to test requesting a security handle
  getmgmtfeature        Command to retrieve the description of a given feature
  getsecctx             Command to test requesting a security context from a handle
  help                  display help
  listmgmtfeatures      Command to retrieve all supported management features
  nvme                  Perform tasks on NVMe controllers
>>>
```

### NVMe subcommand

Operations on NVMe SSD devices are performed using [go-spdk bindings](./go-spdk/README.md) to issue commands over the SPDK framework.

#### NVMe Controller and Namespace Discovery

The following animation illustrates starting the control server and using the management shell to view the NVMe Namespaces discovered on a locally available NVMe Controller (assuming the quickstart_guide instructions have already been performed):

![Demo: List NVMe Controllers and Namespaces](./media/daosshellnamespaces.svg)

#### NVMe Controller Firmware Update

The following animation illustrates starting the control server and using the management shell to update the firmware on a locally available NVMe Controller (assuming the quickstart_guide instructions have already been performed):

![Demo: Updating NVMe Controller Firmware](./media/daosshellfwupdate.svg)

#### NVMe Controller Burn-in Validation

Burn-in validation is performed using the [fio tool](https://github.com/axboe/fio) which executes workloads over the SPDK framework using the [fio_plugin](https://github.com/spdk/spdk/tree/v18.04.1/examples/nvme/fio_plugin).

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
