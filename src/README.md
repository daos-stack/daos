# DAOS Internals

The purpose of this document is to describe the internal code structure and
major algorithms used by DAOS. It assumes prior knowledge of
the <a href="/doc/overview/storage.md">DAOS storage model</a>
and <a href="/doc/overview/terminology.md">acronyms</a>.
This document contains the following sections:

- <a href="#1">DAOS Components</a>
    - <a href="#11">DAOS System</a>
    - <a href="#12">Client APIs, Tools and I/O Middleware</a>
    - <a href="#13">Agent</a>
- <a href="#2">Network Transport and Communications</a>
    - <a href="#21">gRPC and Protocol Buffers</a>
    - <a href="#22">dRPC</a>
    - <a href="#23">CART</a>
- <a href="#3">DAOS Layering and Services</a>
    - <a href="#31">Architecture</a>
    - <a href="#32">Code Structure</a>
    - <a href="#33">Infrastructure Libraries</a>
    - <a href="#34">DAOS Services</a>
- <a href="#4">Software compatibility</a>
    - <a href="#41">Protocol Compatibility</a>
    - <a href="#42">PM Schema Compatibility and Upgrade</a>

<a id="1"></a>
## DAOS Components

As illustrated in the diagram below, a DAOS installation involves several
components that can be either colocated or distributed.
The DAOS software-defined storage (SDS) framework relies on two different
communication channels: an out-of-band TCP/IP network for management and
a high-performant fabric for data access. In practice, the same network
can be used for both management and data access. IP over fabric can also
be used as the management network.

![DAOS SDS Components](/doc/graph/system_architecture.png)

<a id="11"></a>
### DAOS System

A DAOS server is a multi-tenant daemon running on a Linux instance
(i.e. physical node, VM or container) and managing the locally-attached
SCM and NVM storage allocated to DAOS. It listens to a management port, addressed
by an IP address and a TCP port number, plus one or more fabric endpoints,
addressed by network URIs. The DAOS server is configured through a YAML
file (`/etc/daos/daos_server.yml`, or a different path provided on the
command line). Starting and stopping the DAOS server
can be integrated with different daemon management or
orchestration frameworks (e.g. a systemd script, a Kubernetes service or
even via a parallel launcher like pdsh or srun).

A DAOS system is identified by a system name and consists of a set of
DAOS servers connected to the same fabric. Two different systems comprise
two disjoint sets of servers and do not coordinate with each other.
DAOS pools cannot span across multiple systems.

Internally, a DAOS server is composed of multiple daemon processes. The first
one to be started is the <a href="control/README.md">control plane</a>
(binary named `daos_server`) which is responsible for parsing
the configuration file, provisionning storage and eventually starting and
monitoring one or multiple instances of the <a href="iosrv/README.md">data plane</a>
(binary named `daos_io_server`).
The control plane is written in Go and implements the DAOS management API
over the gRPC framework that provides a secured out-of-band channel to
administrate a DAOS system. The number of data plane instances to be started
by each server as well as the storage, CPU and fabric interface affinity can
be configured through the `daos_server.yml` YAML configuration file.

The data plane is a multi-threaded process written in C that runs the DAOS
storage engine. It processes incoming metadata and I/O requests though the
CART communication middleware and accesses local NVM storage via the PMDK
(for storage-class memory, aka SCM) and SPDK (for NVMe SSDs) libraries.
The data plane relies on Argobots for event-based parallel processing and
exports multiple targets that can be independently addressed via the fabric.
Each data plane instance is assigned a unique rank inside a DAOS system.

The control plane and data plane processes communicate locally through Unix
Domain Sockets and a custom lightweight protocol called dRPC.

For further reading:
- <a href="control/README.md">DAOS control plane (daos_server)</a>
- <a href="iosrv/README.md">DAOS data plane (daos_io_server)</a>

<a id="12"></a>
### Client APIs, Tools and I/O Middleware

Applications, users and administrators can interact with a DAOS system
through two different client APIs.

The DAOS management Go package allows to administrate a DAOS system
from any nodes that can communicate with the DAOS servers through the
out-of-band management channel. This API is reserved for the DAOS system
administrators who are authenticated through a specific certificate.
The DAOS management API is intended to be integrated with different
vendor-specific storage management or open-source orchestration frameworks.
A CLI tool called `dmg` is built over the DAOS management API.
For further reading on the management API and the `dmg` tool:
- <a href="https://godoc.org/github.com/daos-stack/daos/src/control/client">DAOS management Go package</a>
- <a href="control/cmd/dmg/README.md">DAOS Management tool (aka dmg)</a>

The DAOS library (`libdaos`) implements the DAOS storage model and is
primarily targeted at application and I/O middleware developers who want
to store datasets into DAOS containers. It can be used from any nodes
connected to the fabric used by the targeted DAOS system. The application
process is authenticated via the DAOS agent (see next section).
The API exported by `libdaos` is commonly called the DAOS API (in contrast
to the DAOS management API) and allows to manage containers and access DAOS
objects through different interfaces (e.g. key-value store or array API).
The `libdfs` library emulates POSIX file and directory abstractions over
`libdaos` and provides a smooth migration path for applications that require
a POSIX namespace. For further reading on `libdaos`, bindings for different
programming languages and `libdfs`:
- <a href="client/api/README.md">DAOS Library (`libdaos`)</a> and <a href="client/array/README.md">array interface</a> and <a href="client/kv/README.md">KV interface</a> built on top of the native DAOS API</a>
- <a href="src/client/pydaos/raw/README.md">Python API bindings</a>
- <a href="https://github.com/daos-stack/go-daos">Go bindings</a> and <a href="https://godoc.org/github.com/daos-stack/go-daos/pkg/daos">API documentation</a>
- <a href="client/dfs/README.md">POSIX File & Directory Emulation (`libdfs`)</a>

The `libdaos` and `libdfs` libraries provide the foundation to support
domain-specific data formats like HDF5 and Apache Arrow. For further reading
on I/O middleware integration, please check the following external references:
- <a href="https://bitbucket.hdfgroup.org/projects/HDFFV/repos/hdf5/browse?at=refs%2Fheads%2Fhdf5_daosm">DAOS VOL connector for HDF5</a>
- <a href="https://github.com/daos-stack/mpich/tree/daos_adio">ROMIO DAOS ADIO driver for MPI-IO</a>

<a id="13"></a>
### Agent

The <a href="control/cmd/daos_agent/README.md">DAOS agent</a> is a daemon
residing on the client nodes. It interacts with the DAOS client library
through dRPC to authenticate the application process. It is a trusted entity
that can sign the DAOS Client credentials using local certificates.
The DAOS agent can support different authentication frameworks and uses a
Unix Domain Socket to communicate with the client library.
The DAOS agent is written in Go and communicates through gRPC with the
control plane component of each DAOS server to provide DAOS system
membership information to the client library and to support pool listing.

<a id="2"></a>
## Network Transport and Communications

As introduced in the previous section, DAOS uses three different
communication channels.

<a id="21"></a>
### gRPC and Protocol Buffers

gRPC provides a bi-directional secured channel for DAOS management.
It relies on TLS/SSL to authenticate the administrator role and the servers.
Protocol buffers are used for RPC serialization and all proto files are
located in the [proto](proto) directory.

<a id="22"></a>
### dRPC

dRPC is communication channel built over Unix Domain Socket that is used
for inter-process communications.
It provides both a [C](common/README.md#dRPC-C-API) and [Go](control/drpc/README.md)
interface to support interactions between:
- the `daos_agent` and `libdaos` for application process authentication
- the `daos_server` (control plane) and the `daos_io_server` (data plane) daemons
Like gRPC, RPC are serialized via protocol buffers.

<a id="23"></a>
### CART

[CART](https://github.com/daos-stack/cart) is a userspace function shipping
library that provides low-latency high-bandwidth communications for the DAOS
data plane. It supports RDMA capabilities and scalable collective operations.
CART is built over [Mercury](https://github.com/mercury-hpc/mercury) and
[libfabric](https://ofiwg.github.io/libfabric/).
The CART library is used for all communications between
`libdaos` and `daos_io_server` instances.

<a id="3"></a>
## DAOS Layering and Services

<a id="31"></a>
### Architecture

As shown in the diagram below, the DAOS stack is structured as a collection
of storage services over a client/server architecture.
Examples of DAOS services are the pool, container, object and rebuild services.

![DAOS Internal Services & Libraries](/doc/graph/services.png)

A DAOS service can be spread across the control and data planes and
communicate internally through dRPC. Most services have client and server
components that can synchronize through gRPC or CART. Cross-service
communications are always done through direct API calls.
Those function calls can be invoked across either the client or server
component of the services. While each DAOS service is designed to be fairly
autonomous and isolated, some are more tightly coupled than others.
That is typically the case of the rebuild service that needs to interact
closely with the pool, container and object services to restore data
redundancy after a DAOS server failure.

While the service-based architecture offers flexibility and extensibility,
it is combined with a set of infrastucture libraries that provide a rich
software ecosystem (e.g. communications, persistent storage access,
asynchronous task execution with dependency graph, accelerator support, ...)
accessible to all the DAOS services.

<a id="32"></a>
### Source Code Structure

Each infrastructure library and service is allocated a dedicated directory
under `src/`. The client and server components of a service are stored in
separate files. Functions that are part of the client component are prefixed
with `dc\_` (stands for DAOS Client) whereas server-side functions use the
`ds\_` prefix (stands for DAOS Server).
The protocol and RPC format used between the client and server components
is usually defined in a header file named `rpc.h`.

All the Go code executed in context of the control plane is located under
`src/control`. Management and security are the services spread across the
control (Go language) and data (C language) planes and communicating
internally through dRPC.

Headers for the official DAOS API exposed to the end user (i.e. I/O
middleware or application developers) are under `src/include` and use the
`daos\_` prefix. Each infrastructure library exports an API that is
available under `src/include/daos` and can be used by any services.
The client-side API (with `dc\_` prefix) exported by a given service
is also stored under `src/include/daos` whereas the server-side
interfaces (with `ds\_` prefix) are under `src/include/daos_srv`.

<a id="33"></a>
### Infrastructure Libraries

The GURT and common DAOS (i.e. `libdaos\_common`) libraries provide logging,
debugging and common data structures (e.g. hash table, btree, ...)
to the DAOS services.

Local NVM storage is managed by the Versioning Object Store (VOS) and
blob I/O (BIO) libraries. VOS implements the persistent index in SCM
whereas BIO is responsible for storing application data in either NVMe SSD
or SCM depending on the allocation strategy. The VEA layer is integrated
into VOS and manages block allocation on NVMe SSDs.

DAOS objects are distributed across multiple targets for both performance
(i.e. sharding) and resilience (i.e. replication or erasure code).
The placement library implements different algorithms (e.g. ring-based
placement, jump consistent hash, ...) to generate the layout of an
object from the list of targets and the object identifier.

The replicated service (RSVC) library finally provides some common code
to support fault tolerance. This is used by the pool, container & management
services in conjunction with the RDB library that implements a replicated
key-value store over Raft.

For further reading on those infrastructure libraries, please see:
- <a href="common/README.md">Common Library</a>
- <a href="vos/README.md">Versioning Object Store (VOS)</a>
- <a href="bio/README.md">Blob I/O (BIO)</a>
- <a href="placement/README.md">Algorithmic object placement</a>
- <a href="rdb/README.md">Replicated database (RDB)</a>
- <a href="rsvc/README.md">Replicated service framework (RSVC)</a>

<a id="34"></a>
### DAOS Services

The diagram below shows the internal layering of the DAOS services and
interactions with the different libraries mentioned above.
![DAOS Internal Layering](/doc/graph/layering.png)

Vertical boxes represent DAOS services whereas horizontal ones are for
infrastructure libraries.

For further reading on the internals of each service:
- <a href="pool/README.md">Pool service</a>
- <a href="container/README.md">Container service</a>
- <a href="object/README.md">Key-array object service</a>
- <a href="rebuild/README.md">Self-healing (aka rebuild)</a>
- <a href="security/README.md">Security</a>

<a id="4"></a>
## Software Compatibility

Interoperability in DAOS is handled via protocol and schema versioning for
persistent data structures.

<a id="41"></a>
### Protocol Compatibility

Limited protocol interoperability is to be provided by the DAOS storage stack.
Version compatibility checks will be performed to verify that:

* All targets in the same pool run the same protocol version.
* Client libraries linked with the application may be up to one
  protocol version older than the targets.

If a protocol version mismatch is detected among storage targets in the same
pool, the entire DAOS system will fail to start up and will report failure
to the control API. Similarly, connection from clients running a protocol
version incompatible with the targets will return an error.

<a id="42"></a>
### PM Schema Compatibility and Upgrade

The schema of persistent data structures may evolve from time to time to
fix bugs, add new optimizations or support new features. To that end,
the persistent data structures support schema versioning.

Upgrading the schema version is not done automatically and must be initiated
by the administrator. A dedicated upgrade tool will be provided to upgrade
the schema version to the latest one. All targets in the same pool must have
the same schema version. Version checks are performed at system initialization
time to enforce this constraint.

To limit the validation matrix, each new DAOS release will be published
with a list of supported schema versions. To run with the new DAOS release,
administrators will then need to upgrade the DAOS system to one of the
supported schema version. New target will always be reformatted with the
latest version. This versioning schema only applies to data structure
stored in persistent memory and not to block storage that only stores
user data with no metadata.

