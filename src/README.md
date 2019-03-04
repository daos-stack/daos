# DAOS Internals

The purpose of this document is to describe the internal code structure and major algorithms used by DAOS. It assumes prior knowledge of the <a href="/doc/storage_model.md">DAOS storage model</a> and <a href="/doc/terminology.md">acronyms</a>.
This document contains the following sections:

- <a href="#1">DAOS Components</a>
    - <a href="#11">DAOS System</a>
    - <a href="#12">Client APIs, Tools and I/O Middleware</a>
    - <a href="#13">Agent</a>
- <a href="#2">Network Transport and Communications</a>
    - <a href="#21">gRPC and Protocol Buffers</a>
    - <a href="#22">dRPC</a>
    - <a href="#23">CART</a>
- <a href="#3">DAOS Layering and Microservices</a>
    - <a href="#31">Architecture</a>
    - <a href="#32">Code Structure</a>
    - <a href="#33">Infastructure & Microservices</a>
- <a href="#4">Software compatibility</a>
    - <a href="#41">Protocol Compatibility</a>
    - <a href="#42">PM Schema Compatibility and Upgrade</a>

<a id="1"></a>
## DAOS Components

A DAOS installation involves several components that can be either colocated or distributed. The DAOS software-defined storage framework relies on two different communication channels: an out-of-band TCP/IP network for management and a high-performant fabric for data access. In practice, the same network can be used for both management and data access. IP over fabric can also be used as the management network.

<a id="11"></a>
### DAOS System

A DAOS server is a multi-tenant daemon running on a Linux instance (i.e. physical node, VM or container) and managing the locally-attached NVM storage allocated to DAOS. It listens to a management port, addressed by an IP address and a TCP port number, plus one or more fabric endpoints, addressed by network URIs. The DAOS server is configured through a YAML file (i.e. /etc/daos_server.yml or different path provided on the commmand line) and can be integrated with different daemon management or orchestration frameworks (e.g. a systemd script, a Kunernetes service or even via a parallel launcher like pdsh).

A DAOS system is identified by a system name and consists of a set of DAOS servers connected to the same fabric. Two different systems comprise two disjoint sets of servers and do not coordinate with each other. DAOS pools cannot span across multiple systems.

Internally, a DAOS server is composed of multiple daemon processes. The first one to be started is the <a href="control/README.md">control plane</a> (binary named daos_server for convenience) which is responsible for parsing the configuration file, provisionning storage and eventually starting and monitoring one or mutiple instances of the <a href="iosrv/README.md">data plane</a> (i.e. daos_io_server binary). The control plane is written in Go and implements the DAOS management API over the gRPC framework that provides a secured out-of-band channel to administrate a DAOS system. The number of data plane instances to be started by each server as well as the storage, CPU and fabric interface affinity can be configured through the YAML configuration file.

The data plane is a multi-threaded process written in C that runs the DAOS storage engine. It processes incoming metadata and I/O requests though the CART communication middleware and accesses local NVM storage via the PMDK (for storage-class memory, aka SCM) and SPDK (for NVMe SSDs) libraries. The data plane relies on Argobots for event-based parallel processing and exports multiple targets that can be independently addressed via the fabric. Each data plane instance is assigned a unique rank inside a DAOS system.

The control plane and data plane processes communicate locally through Unix Domain Sockets and a custom lightweight protocol called dRPC.

For further reading:
- <a href="control/README.md">DAOS control plane (daos_server)</a>
- <a href="iosrv/README.md">DAOS data plane (daos_io_server)</a>

<a id="12"></a>
### Client APIs, Tools and I/O Middleware

Applications, users and administrators can interact with a DAOS system through two different client APIs.

The DAOS management Go package allows to administrate a DAOS system from any nodes that can communicate with the DAOS servers through the out-of-band management channel. This API is reserved for the DAOS system administrators who are authenticated through a specific certificate. The DAOS management API is intended to be integrated with different vendor-specific storage management or open-source orchestration frameworks. A CLI tool called dmg is built over the DAOS management API. For further reading on the management API and the dmg tool:
- <a href="https://godoc.org/github.com/daos-stack/daos/src/control/client">DAOS management Go package</a>
- <a href="control/dmg/README.md">DAOS Management tool (aka dmg)</a>

The DAOS library (i.e. libdaos) implements the DAOS storage model and is primarily targeted at application and I/O middleware developers who want to store datasets into DAOS containers. It can be used from any nodes connected to the fabric used by the targeted DAOS system. The application process is authenticated via the DAOS agent (see next section). The API exported by libdaos is commonly called the DAOS API (in opposition to the DAOS management API) and allows to manage containers and access DAOS objects through different interfaces (e.g. key-value store or array API). The libdfs library emulates POSIX file and directory abstractions over libdaos and provides a smooth migration path for applications that require a POSIX namespace. For further reading on libdaos, bindings for different programming languages and libdfs:
- <a href="client/api/README.md">DAOS Library (libdaos)</a>
- <a href="utils/py/README.md">Python API bindings</a>
- <a href="https://github.com/daos-stack/go-daos">Go bindings</a> and <a href="https://godoc.org/github.com/daos-stack/go-daos/pkg/daos">API documentation</a>
- <a href="client/dfs/README.md">POSIX File & Directory Emulation (libdfs)</a>

The libdaos and libdfs libraries provide the foundation to support domain-specific data formats like HDF5 and Apache Arrow. For further reading on I/O middleware integration, please check the following external references:
- <a href="https://bitbucket.hdfgroup.org/projects/HDFFV/repos/hdf5/browse?at=refs%2Fheads%2Fhdf5_daosm">DAOS VOL connector for HDF5</a>
- <a href="https://github.com/daos-stack/mpich/tree/daos_adio">ROMIO DAOS ADIO driver for MPI-IO</a>

<a id="13"></a>
### Agent

The <a href="control/agent/README.md">DAOS agent</a> is a daemon residing on the client node and interacts with the DAOS client library through dRPC to authenticate the application process. It is a trusted entity that can sign the DAOS Client credentials using local certificates. The agent can support different authentication frameworks and uses a Unix Domain Socket to communicate with the client library. The DAOS agent is written in Go and communicates through gRPC with the control plane component of each DAOS server to provide DAOS system membership information to the client library and to support pool listing.

<a id="2"></a>
## Network Transport and Communications

As introduced in the previous section, DAOS uses three different communication channels.

<a id="21"></a>
### gRPC and Protocol Buffers

gRPC provides a bi-directional secured channel for DAOS management. It relies on TLS/SSL to authenticate the administrator role and the servers. Protocol buffers are used for RPC serialization and all proto files are located in the [proto](proto) directory.

<a id="22"></a>
### dRPC

dRPC is communication channel built over Unix Domain Socket that is used for inter-process communications. It provides both a [C](common/README.md#dRPC-C-API) and [Go](control/drpc/README.md) interface to support interactions between:
- the agent and libdaos for application process authentication
- the daos_server (control plane) and the daos_io_server (data plane) daemons
Like gRPC, RPC are serialized via protocol buffers.

<a id="23"></a>
### CART

[CART](https://github.com/daos-stack/cart) is a userspace function shipping library that provides low-latency high-bandwidth communications for the DAOS data plane. It supports RDMA capabilities and scalable collective operations. CART is built over [Mercury](https://github.com/mercury-hpc/mercury) and [libfabric](https://ofiwg.github.io/libfabric/). The CART library is used for all communications between libdaos and daos_io_server instances.

<a id="3"></a>
## DAOS Layering and Microservices

<a id="31"></a>
### Architecture

The DAOS stack is organized as a set of microservices over a client/server architecture. The <a href="#5a">figure</a> below shows the logical layering of the DAOS stack.

<a id="32"></a>
### Code Structure

<a id="33"></a>
### Infastructure & Microservices

- <a href="vos/README.md">Versioning Object Store</a>
- <a href="bio/README.md">Blob I/O</a>
- <a href="rdb/README.md">Service Replication</a>
- <a href="pool/README.md">Pool Service</a>
- <a href="container/README.md">Container Service</a>
- <a href="object/README.md">Key Array Object</a>
- <a href="placement/README.md">Algorithmic Object Placement</a>
- <a href="rebuild/README.md">Self-healing</a>
- <a href="security/README.md">Security</a>
- <a href="client/api/README.md">DAOS Client Library</a>
- <a href="client/dfs/README.md">POSIX File & Directory Emulation</a>
- <a href="common/README.md">Common Library</a>

<a id="4"></a>
## Software Compatibility

Interoperability in DAOS is handled via protocol and schema versioning for persistent data structures.

<a id="41"></a>
### Protocol Compatibility

Limited protocol interoperability is to be provided by the DAOS storage stack. Version compatibility checks will be performed to verify that:

* All targets in the same pool run the same protocol version.
* Client libraries linked with the application may be up to one protocol version older than the targets.

If a protocol version mismatch is detected among storage targets in the same pool, the entire DAOS system will fail to start up and will report failure to the control API. Similarly, connection from clients running a protocol version incompatible with the targets will return an error.

<a id="42"></a>
### PM Schema Compatibility and Upgrade

The schema of persistent data structures may evolve from time to time to fix bugs, add new optimizations or support new features. To that end, the persistent data structures support schema versioning.

Upgrading the schema version is not done automatically and must be initiated by the administrator. A dedicated upgrade tool will be provided to upgrade the schema version to the latest one. All targets in the same pool must have the same schema version. Version checks are performed at system initialization time to enforce this constraint.

To limit the validation matrix, each new DAOS release will be published with a list of supported schema versions. To run with the new DAOS release, administrators will then need to upgrade the DAOS system to one of the supported schema version. New target will always be reformatted with the latest version. This versioning schema only applies to data structure stored in persistent memory and not to block storage that only stores user data with no metadata.
