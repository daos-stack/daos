# DAOS Server

The server package implements the internals of the DAOS control server,
and the `daos_server` user-facing application is implemented by the
[`daos_server`](/src/control/cmd/daos_server/README.md) package.

## I/O Server Instances

DAOS I/O Server processes (`daos_io_server` binary) are forked
by the DAOS Control Server (`daos_server` binary) and perform the
main userspace I/O operations of DAOS.
[`instance.go`](/src/control/server/instance.go) provides the
`IOServerInstance` abstraction and relevant methods.

Underlying abstractions for the control and configuration of
the I/O Server processes are encapsulated in the
[`ioserver`](/src/control/server/ioserver) package.

## I/O Server Harness

DAOS I/O Server processes are managed and monitored by the DAOS
Control Server and logically reside as members of the I/O server
harness.
[`harness.go`](/src/control/server/harness.go) provides the
`IOServerHarness` abstraction and relevant methods.

## Communications

The DAOS Server implements the [gRPC protocol](https://grpc.io/) to
communicate with client gRPC applications and interacts with DAOS I/O
Servers through Unix domain sockets.

Multiple gRPC server modules are loaded by the control server.
Currently included modules are security and management.

Control plane server (`daos_server`) instances will open a gRPC
channel to listen for requests from control plane client
applications.

[`server.go`](/src/control/server/server.go) contains main setup
routines, including the establishment of the gRPC server and registering
of RPCs.

### Control Service

The gRPC server registers the
[control service](/src/proto/ctl/control.proto) to handle requests
from the management tool.

Control service requests are operations that will be performed on
one or more nodes in parallel, such as hardware provisioning, which
will execute through storage or network libraries.

Such broadcast commands (which will be issued after connecting to a
list of hosts) will usually be issued by the
[management tool](/src/control/cmd/dmg/README.md), a gRPC client.

These commands will not trigger dRPCs but will perform node-local
functions such as hardware (network and storage) provisioning.

Control service RPC handler code is contained in
`/src/control/server/ctl_*.go` files and protobuf specific
un/wrapping code in `/src/control/server/ctl_*_rpc.go` files.

### Management Service

The Control Plane implements a management service as part of the DAOS
Server, responsible for handling distributed operations across the
DAOS System.

MS commands (which will be issued when connecting to a
access point host) will usually be issued by the
[management tool](/src/control/cmd/dmg/README.md), a gRPC client.

MS commands will be triggered from the management tool and handled
on a storage node running as an access point.
Requests will be forwarded to the data plane ([iosrv](/src/iosrv))
over dRPC channel and handled by the [mgmt](/src/mgmt/srv.c) module.

Management service RPC handler code is contained in
[`mgmt_svc.go`](/src/control/server/mgmt_svc.go).

### Fanout

Some control service RPC handlers will trigger fanout to multiple
remote harnesses over gRPC, in order to send these fanout requests
the `mgmtClient` is used as defined in
[`mgmt_client.go`](/src/control/server/mgmt_client.go).

An example of a management tool command that executes gRPC fanout
over multiple remote harnesses is `dmg system query`, the server side
handler for which is `SystemQuery` in
[`ctl_system.go`](/src/control/server/ctl_system.go) which issues
requests to remote harnesses using the `HarnessClient` abstraction in
[`ctl_harness.go`](/src/control/server/ctl_harness.go) which in turn
sends requests with the `mgmtClient` as described above.

### Storage

Storage related RPCs, whose handlers are defined in
[`ctl_storage*.go`](/src/control/server/ctl_storage.go)
delegate operations to backend providers encapsulated in the `bdev`
and `scm`
[storage subpackages](/src/control/server/storage/).

## Bootstrapping and DAOS system membership

When starting a data-plane instance, we look at the superblock to
determine whether it should be a MS (management service) replica.
The `daos_server.yml`'s `access_points` parameter is used (only
during format) to determine whether an instance is to be a MS replica
or not.

When the starting instance is identified as an MS replica, it
performs bootstrap and starts.  If the DAOS system has only one
replica (as specified by `access_points` parameter), the host of the
bootstrapped instance is now the MS leader.  Whereas if there are
multiple replicas, elections will happen in the background and
eventually a leader will be elected.

When the starting instance is not identified as an MS replica, the
instance's host calls Join on MgmtSvcClient over gRPC including the
instance's host ControlAddress (address that the gRPC server is
listening on) in the request addressed to the MS leader.

MgmtSvc running on the MS leader handles the Join request received by
gRPC server and forwards request over dRPC to the MS leader instance.
If the Join request is successful then the MS leader MgmtSvc records
the address contained in the request as a new system member.

## Storage Management

Operations on NVMe SSD devices are performed using
[go-spdk bindings](/src/control/lib/spdk)
to issue commands through the SPDK framework native C libraries.

Operations on SCM persistent memory modules are performed using
[go-ipmctl bindings](/src/control/lib/ipmctl)
to issue commands through the ipmctl native C libraries.

### Storage Format

Storage is required to be formatted before the DAOS data plane can be
started.

![Storage format diagram](/doc/graph/storage_format_detail.png)

If storage has not been previously formatted, `daos_server` will
halt on start-up waiting for storage format to be
[triggered](/src/control/cmd/dmg/README.md) through the management
tool.

Storage format is expected only to be performed when setting up the
DAOS system for the first time.

#### SCM Format

Formatting SCM involves creating an ext4 filesystem on the nvdimm
device.  Mounting SCM results in an active mount using the DAX
extension enabling direct access without restrictions imposed by
traditional block storage.

Formatting and mounting of SCM device namespace is performed as
specified in config file parameters prefixed with `scm_`.

#### NVMe Format

In the context of what is required from the control plane to prepare
NVMe devices for operation with DAOS data plane, "formatting" refers
to the reset of storage media which will remove blobstores and remove
any filesystem signatures from the SSD controller namespaces.

Formatting will be performed on devices identified by PCI addresses
specified in config file parameter `bdev_list` when `bdev_class` is
equal to `nvme`.

In order to designate NVMe devices to be used by DAOS data plane
instances, the control plane will generate a `daos_nvme.conf` file to
be consumed by SPDK which will be written to the `scm_mount`
(persistent) mounted location as a final stage of formatting before
the superblock is written, signifying the server has been formatted.

## Architecture

A view of DAOS' software component architecture:

![Architecture diagram](/doc/graph/system_architecture.png)

## Running

For instructions on building and running DAOS see the
[admin guide](https://daos-stack.github.io/admin/installation/).

## Configuration

For instructions on configuring the DAOS server see the
[admin
guide](https://daos-stack.github.io/admin/deployment/#server-configuration-file).

