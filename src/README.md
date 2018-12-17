# DAOS Internals

The purpose of this document is to describe the internal code structure and major algorithms used by DAOS. It assumes prior knowledge of the <a href="/doc/storage_model.md">DAOS storage model</a> and <a href="/doc/terminology.md">acronyms</a>.
This document contains the following sections:

- <a href="#1">DAOS Components</a>
    - <a href="#2">Storage Engine</a>
        - <a href="control/README.md">DAOS control server (control plane)</a>
        - <a href="iosrv/README.md">DAOS I/O server (data plane)</a>
    - <a href="#3">Client Library</a>
        - <a href="client/README.md">DAOS Client library and I/O Middleware</a>
    - <a href="#4">Agent</a>
        - <a href="control/agent/README.md">DAOS Agent</a>
    - <a href="#5">Management Tool</a>
        - <a href="control/dmg/README.md">DAOS Management tool (aka dmg)</a>
- <a href="#100">Network Transport & Communications</a>
- <a href="#200">DAOS Services</a>
    - <a href="rdb/README.md">Service Replication</a>
    - <a href="pool/README.md">Pool Service</a>
    - <a href="container/README.md">Container Service</a>
    - <a href="object/README.md">Key Array Object</a>
    - <a href="vos/README.md">Versioning Object Store</a>
    - <a href="bio/README.md">Blob I/O</a>
    - <a href="placement/README.md">Algorithmic Object Placement</a>
    - <a href="rebuild/README.md">Self-healing</a>
    - <a href="security/README.md">Security</a>
    - <a href="common/README.md">Common Library</a>
- <a href="#300">Software compatibility</a>
    - <a href="#301">Protocol Compatibility</a>
    - <a href="#302">PM Schema Compatibility and Upgrade</a>

<a id="1"></a>
## System Components

A DAOS system is a multi-server installation with several components.

<a id="2"></a>
### Storage Engine
- DAOS Server (control plane): Go (privileged) process managed by systemd (or other). daos_server instance running on single storage server. Can spawn multiple daos_io_servers (data plane) but mostly opaque to user.
- DAOS I/O Server (data plane): The DAOS server is a persistent service running on all the storage nodes. It manages incoming requests for all targets hosted on the storage node and provides a stackable modular interface to load server-side modules on demand. Initially, only certified server-side modules can be loaded to address security concerns. In the future, untrusted modules can run in a sandbox managed by control groups (i.e. cgroup), allowing a fine-grained control over the resources (memory, CPU cycles, etc.) consumed by the module.  A server module can register handlers for processing RPCs issued by a counterpart client library or other instance of the same module on different servers. The DAOS server provides service threads with core and NUMA affinity to execute those RPC handlers. Service threads rely on event-based processing, which means that each service thread can manage multiple concurrent requests simultaneously. Each module may call into the external API of the layer below to eventually access the local versioning object store library in charge of data persistency.

<a href="control/README.md">DAOS control server (control plane)</a>
<a href="iosrv/README.md">DAOS I/O server (data plane)</a>

<a id="3"></a>
### Client
- DAOS Client: Resides on client node. Application that links to libdaos and talks to DAOS Agent. DAOS provides a collection of client libraries (one per-layer) that implement the external API exported to application developers. The DAOS client libraries as well as the networking parts do not spawn any internal threads and can be linked directly with the application. It supports the event and event queue interface for non-blocking operations. During execution, a client library can either call into the client library of the lower layer, or use the network transport to send a RPC to its server counterpart, as show in Figure 4 2.

<a href="client/README.md">DAOS Client and I/O Middleware</a>

<a id="4"></a>
### Agent
- DAOS Agent: daos_agent is a daemon process residing on the client node. Trusted intermediary in authentication of DAOS Client application. Signs DAOS Client credentials using local certificates

<a href="control/agent/README.md">DAOS Agent</a>

<a id="5"></a>
### Management Tool
- DAOS Management Tool: Uses control plane client API to administrate the daos_server instances. e.g. daos_shell executable

<a href="control/dmg/README.md">DAOS Management tool (aka dmg)</a>

<a id="100"></a>
## Network Transport & Communications

Network Transport with CaRT & gRPC. Node Addressing:
- Nodes within a Mercury process group are addressed by endpoints that are assigned for the lifetime of the session. DAOS, on the other hand, must store persistent node identifiers in the pool map. Storage targets will therefore need to convert the persistent node identifiers into runtime addresses when transferring the pool map to other targets and clients. RPCs will then be sent through Mercury by specifying the endpoint of the storage node inside the process group. Likewise, any updates to the pool map will require converting the Mercury endpoint into a persistent node identifier.
- gRPC

<a id="200"></a>
## DAOS Services

The DAOS stack is organized as a set of micro-services over a client/server architecture. The <a href="#5a">figure</a> below shows the logical layering of the DAOS stack.

<a id="5a"></a>
![/doc/graph/Fig_005.png](/doc/graph/Fig_005.png "DAOS Logical Layering")

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
- <a href="client/dfs/README.md">POSIX File & Direcotory Emulation</a>
- <a href="common/README.md">Common Library</a>

<a id="300"></a>
## Software Compatibility

Interoperability in DAOS is handled via protocol and schema versioning for PM data structures.

<a id="301"></a>
### Protocol Compatibility

Limited protocol interoperability is to be provided by the DAOS storage stack. Version compatibility checks will be performed to verify that:

* All targets in the same pool run the same protocol version.
* Client libraries linked with the application may be up to one protocol version older than the targets.

If a protocol version mismatch is detected among storage targets in the same pool, the entire pool will fail to start up and will report failure to the control API. Similarly, connection from clients running a protocol version incompatible with the targets will return an error.

<a id="302"></a>
### PM Schema Compatibility and Upgrade

The schema of persistent data structures might evolve from time to time to fix bugs, add new optimizations or support new features. To that end, VOS supports schema versioning.

Upgrading the schema version is not done automatically and must be initiated by the administrator. A dedicated upgrade tool will be provided to upgrade the schema version to the latest one. All targets in the same pool must have the same schema version. Version checks are performed at pool initialization time to enforce this constraint.

To limit the validation matrix, each new DAOS release will be published with a list of sup-ported schema versions. To run with the new DAOS release, administrators will then need to upgrade the pools to one of the supported schema version. New target will always be reformatted with the latest version. This versioning schema only applies to data structure stored in persistent memory and not to block storage that only stores data buffers with no metadata.
