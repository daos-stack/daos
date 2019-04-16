# DAOS Control Plane (aka daos_server)

DAOS operates over two, closely integrated planes, Control and Data. The Data plane handles the heavy lifting transport operations while the Control plane orchestrates process and storage management, facilitating the operation of the Data plane.

[DAOS Server](server) implements the DAOS Control Plane and is written in Golang. It is tasked with network and storage hardware provisioning and allocation in addition to instantiation and management of the DAOS IO Servers (Data Plane written in C) running on the same host. Users of DAOS will interact directly only with the Control Plane in the form of the DAOS Server and associated tools.

The DAOS Server implements the [gRPC protocol](https://grpc.io/) to communicate with client gRPC applications and interacts with DAOS IO Servers through Unix domain sockets.

Multiple gRPC server modules are loaded by the control server. Currently included modules are security and management.

The Control Plane implements a replicated management service as part of the DAOS Server, responsible for handling distributed operations across the DAOS System.

The [management tool](dmg) is an example client application which can connect to both the [agent](agent) to perform security functions (such as providing credentials and retrieving security contexts) and to the local management server to perform management functions (such as storage device discovery).

## Documentation

- [Management API](https://godoc.org/github.com/daos-stack/daos/src/control/client)
- [Management internals](https://godoc.org/github.com/daos-stack/daos/src/control/server)
- [Agent API](https://godoc.org/github.com/daos-stack/daos/src/control/client/agent)
- [Agent internals](https://godoc.org/github.com/daos-stack/daos/src/control/security)
- [dRPC](https://godoc.org/github.com/daos-stack/daos/src/control/drpc)
- [server package](server/README.md)
- [management tool package](dmg/README.md)
- [client package](client/README.md)
- [common package](common/README.md)

## Architecture

First a view of software component architecture:

![Architecture diagram](/doc/graph/system_architecture.png)

## Development Requirements

- [Golang](https://golang.org/) 1.9 or higher
- [gRPC](https://grpc.io/)
- [Protocol Buffers](https://developers.google.com/protocol-buffers/)
- [Dep](https://github.com/golang/dep/) for managing dependencies in vendor directory.

## Development setup

- If changing vendor package versions, edit `src/control/Gopkg.toml` and then run `dep ensure` from src/control.
- (Optional) protoc protocol buffer compiler

### Building the app

For instructions on building and running DAOS see the [Quickstart guide](../../doc/quickstart.md).

Build with `scons` and binaries should be produced in `install/bin` directory.

### Testing the app

Run the tests `go test` within each directory containing tests

### Run unit tests locally

Checkout the DAOS source code:

`git clone <https://github.com/daos-stack/daos.git>`

Checkout the SPDK source code on branch v18.07.x:

`git clone --single-branch --branch v18.07.x git@github.com:spdk/spdk.git`

Continue installing SPDK with the procedure in the repository on branch v18.07.x: [SPDK-v18.07.x](https://github.com/spdk/spdk/tree/v18.07.x)

Setup environment variables:

```bash
DAOS_REPO="/path/to/daos_repo"
SPDK_REPO="/path/to/spdk_repo"
export CGO_LDFLAGS="-L${SPDK_REPO}/build/lib"
export CGO_CFLAGS=-I${SPDK_REPO}/include
export LD_LIBRARY_PATH="${SPDK_REPO}/build/lib:${DAOS_REPO}/src/control/vendor/github.com/daos-stack/go-spdk/spdk"
```

Build NVME libs:

```bash
cd ${DAOS_REPO}/src/control/vendor/github.com/daos-stack/go-spdk/spdk
gcc ${CGO_LDFLAGS} ${CGO_CFLAGS} -Werror -g -Wshadow -Wall -Wno-missing-braces -c -fpic -Iinclude src/*.c -lspdk
gcc ${CGO_LDFLAGS} ${CGO_CFLAGS} -shared -o libnvme_control.so *.o
```

To run suite of control plane unit tests:

```bash
cd ${DAOS_REPO}/src/control
./run_go_tests.sh
```

To run go-spdk tests:

```base
cd ${DAOS_REPO}/src/control/vendor/github.com/daos-stack/go-spdk/spdk
go test -v
```

## Coding Guidelines

### daos_server and daos_agent

Avoid calling `os.Exit` (or function with equivalent effects), except for assertion purposes. Fatal errors shall be returned back to `main`, who calls `os.Exit` accordingly.
