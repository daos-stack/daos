# DAOS Server

The server package implements the internals of the DAOS Server, and the
`daos_server` user-facing application is implemented by the
[`daos_server`](/src/control/cmd/daos_server/README.md) package.

## I/O Engine Instances

DAOS I/O Engine processes (`daos_engine` binary) are forked by the DAOS Control
Server (`daos_server` binary) and perform the main userspace I/O operations of
DAOS.
[`instance.go`](/src/control/server/instance.go) provides the `EngineInstance`
abstraction and relevant methods.

Underlying abstractions for the control and configuration of the I/O Engine
processes are encapsulated in the [`engine`](/src/control/server/engine)
package.

## I/O Engine Harness

DAOS I/O Engine processes are managed and monitored by the DAOS Server and
logically reside as members of the I/O Engine harness.
[`harness.go`](/src/control/server/harness.go) provides the `EngineHarness`
abstraction and relevant methods.

## Communications

The DAOS Server implements the [gRPC protocol](https://grpc.io/) to communicate
with client gRPC applications and interacts with DAOS I/O Engines through Unix
domain sockets.

Multiple gRPC server modules are loaded by DAOS Server, currently included
modules are security and management.

DAOS Server (`daos_server`) instances will open a gRPC channel to listen for
requests from control-plane client applications and other DAOS Server
instances.

[`server.go`](/src/control/server/server.go) contains main setup routines,
including the establishment of the gRPC server and registering of RPCs.

### Control API

The [control](/src/control/lib/control/README.md) package exposes an RPC-based
API for control-plane client applications to communicate with DAOS Server
processes.

### Protobuf Definitions

Protobuf definitions are described in the [proto](/src/proto/README.md)
directory.

### Control Service

The gRPC server registers the [control service](/src/proto/ctl/control.proto)
to handle requests from the management tool.

Control service requests are operations that will be performed on one or more
`daos_server` processes in parallel, such as hardware provisioning. The
handlers triggered on receipt of control service RPCs will typically end-up
calling into native-C storage or network libraries through the relevant
go bindings e.g. [ipmctl](/src/control/lib/ipmctl/README.md),
[spdk](/src/control/lib/ipmctl/README.md) or
[hardware](/src/control/lib/hardware).

Such broadcast commands (which will be issued after connecting to a list of
hosts) will usually be issued by the
[management tool](/src/control/cmd/dmg/README.md), a gRPC client that
communicates with `daos_server` processes through the control API.

These commands will not usually trigger dRPCs and will mostly perform functions
such as hardware (network and storage) provisioning.

The control service RPC handler code is contained in
`/src/control/server/ctl_*.go` files and protobuf specific un/wrapping code in
`/src/control/server/ctl_*_rpc.go` files.

### Management Service

The Control Plane implements a management service as part of the DAOS Server,
responsible for handling distributed operations across the DAOS System.

Some `dmg` commands will trigger MS requests to be issued to a `daos_server`
process on a storage node running as the MS leader, this happens under the hood
and the logic for the request steering is handled in the control API which is
utilized by the [`dmg`](/src/control/cmd/dmg/README.md) tool.

When necessary, requests will be forwarded to the data plane
[engine](/src/engine/README.md) over dRPC channel and handled by the
[mgmt](/src/mgmt/srv.c) module.

MS RPC related code is contained in `/src/control/server/mgmt_*.go` files.

### Server-to-server Fan-out

Some control service RPC handlers will trigger fan-out to multiple remote
harnesses over gRPC, in order to send these fan-out requests the client in the
[control API](/src/control/lib/control/README.md) is used.

An example of a management tool command that executes gRPC fan-out over multiple
remote harnesses is `dmg system stop`, the server side handler for which is
`SystemStop` in [`ctl_system.go`](/src/control/server/ctl_system.go) which
issues requests to remote harnesses.
The use of the control API client (which implements the UnaryInvoker interface)
to issue a fan-out request is demonstrated in
[`system.go`](/src/control/lib/control/system.go) `SystemStop` client call.

### System Command Handling

System commands use fan-out and send unary RPCs to selected ranks across the
system for actions stop, start and reformat.

### Storage Command Handling

Storage related RPCs, whose handlers are defined in
[`ctl_storage*.go`](/src/control/server/ctl_storage.go)
delegate operations to backend providers encapsulated in the `bdev` and `scm`
[storage subsystem packages](/src/control/server/storage/).

## Bootstrapping and DAOS System Membership

When starting a data-plane instance, we look at the superblock to determine
whether the instance should be started as a MS (management service) replica.
The `daos_server.yml`'s `access_points` parameter is used (only during format)
to determine whether an instance is to be a MS replica or not.

When the starting instance is identified as an MS replica, it performs
bootstrap and starts.
If the DAOS system has only one replica (as specified by `access_points`
parameter), the host of the bootstrapped instance is now the MS leader.
Whereas if there are multiple replicas, elections will happen in the background
and eventually a leader will be elected.

When the starting instance is not identified as an MS replica, the instance's
host calls Join on the control API client which triggers a gRPC request to
the MS leader.
The joining instance's control address is populated in the request.

The gRPC server running on the MS leader handles the Join request and allocates
a DAOS system rank which is recorded in the MS membership (which is backed by
the distributed system database).
The rank is returned in the Join response and communicated to the data-plane
(engine) over dRPC.

## Storage Management

Operations on NVMe SSD devices are performed using
[go-spdk bindings](/src/control/lib/spdk) to issue commands through the SPDK
framework native C libraries.

Operations on SCM persistent memory modules are performed using
[go-ipmctl bindings](/src/control/lib/ipmctl) to issue commands through the
ipmctl native C libraries.

Storage RPC related code which concerns the server-side handling of requests
is contained within `/src/control/server/ctl_storage*.go` files.

### Storage Format

Storage is required to be formatted before the DAOS data plane can be
started.

![Storage format diagram](/docs/graph/storage_format_detail.png)

If storage has not been previously formatted, `daos_server` will halt on
start-up waiting for storage format to be triggered by issuing the `dmg storage
format` command.

Storage format is expected only to be performed when setting up the DAOS system
for the first time.

#### SCM Format

Formatting SCM involves creating an ext4 filesystem on the nvdimm device.
Mounting SCM results in an active mount using the DAX extension enabling direct
access without restrictions imposed by traditional block storage.

Formatting and mounting of SCM device namespace is performed as specified in
config file parameters prefixed with `scm_`.

#### NVMe Format

In the context of what is required from the control plane to prepare NVMe
devices for operation with DAOS data plane, "formatting" refers to the reset of
storage media which will remove blobstores and remove any filesystem signatures
from the SSD controller namespaces.

Formatting will be performed on devices identified by PCI addresses specified
in config file parameter `bdev_list` when `bdev_class` is equal to `nvme`.

In order to designate NVMe devices to be used by DAOS data plane instances, the
control plane will generate a `daos_nvme.conf` file to be consumed by SPDK
which will be written to the `scm_mount` (persistent) mounted location as a
final stage of formatting before the superblock is written, signifying the
server has been formatted.

## Architecture

A view of DAOS' software component architecture:

![Architecture diagram](/docs/graph/system_architecture.png)

## Running

For instructions on building and running DAOS see the
[admin guide](https://daos-stack.github.io/admin/installation/).

## Configuration

For instructions on configuring the DAOS server see the
[admin guide](https://daos-stack.github.io/admin/deployment/#server-configuration-file).

