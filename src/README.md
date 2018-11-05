# DAOS Internals

This document contains the following sections:

- [DAOS Layering](51)
- <a href="#52">Client/Server Architecture</a>
    -  <a href="#53">DAOS Common Libraries</a>
    - <a href="#54">DAOS Client Libraries</a>
    - <a href="#55">DAOS Server and Server Modules</a>
- <a href="#58">Network Transport</a>
    - <a href="#59">Mercury Function Shipper</a>
    - <a href="#60">Multi-layer Function Shipping</a>
    - <a href="#61">Node Addressing</a>
- <a href="#62">Persistent Memory based Storage</a>
    - <a href="#63">In-Memory Storage</a>
    - <a href="#64">Lightweight I/O Stack: NVM Library</a>
 - <a href="#65">Software compatibility</a>
    - <a href="#66">Protocol Compatibility</a>
    - <a href="#67">PM Schema Compatibility and Upgrade</a>

<a id="51"></a>
## DAOS Layering

The DAOS stack is organized in four different layers. Each layer provides a unique set of functionality based on the abstraction offered by the layer below. The layered architecture relies on a top-down dependency structure that allows each layer to be usable, independently of the upper layers. Application and middleware can then plug into any layer of the stack, depending on the level of functionality required. This architecture is also very convenient for testing, debugging, and performance analysis. The <a href="#5a">figure</a> below shows the logical layering of the DAOS stack.

<a id="5a"></a>
![/doc/graph/Fig_005.png](/doc/graph/Fig_005.png "DAOS Logical Layering")

The Versioning Object Store (VOS) is the essence of the DAOS object model. It provides object storage local to a storage node and represents the foundation of the storage target. A VOS instance supports multiple object address spaces and maintains multiple concurrent versions of each address space. VOS uses a persistent-memory storage model for all VOS metadata, and may use either persistent memory or block storage for VOS data. VOS logs all object update operations and maintains internal indexes to integrate object reads at arbitrary epochs efficiently. VOS also supports epoch aggregation to collapse the history, simplify metadata, and reclaim space.

DAOS-M (DSM) stands for memory-class DAOS. While VOS focuses on issues local to one storage node, the DAOS-M layer implements a DSM client library that communicates with different types of DSM servicesâone of which exports the VOS methodsâand allows callers to interact with the distributed pool and container abstractions. A DSM pool manages pool connections, authentication and authorization, target membership, etc., and stores per-container metadata for upper layers. A DSM container manages container handles, distributed epoch state, etc., but provides only target-local objects. I/O accesses to these local objects go almost transparently to VOS with very little overhead. The DAOS-M layer runs the pool and container metadata services, which are replicated with strong consistency and fault tolerance. Object storage is partitioned over storage targets with no redundancy.

DAOS-SR (DSR) stands for the DAOS sharding and resilience layer. It provides a global object address space for each container and distributes object data across multiple DAOS-M objects stored on different targets, to achieve horizontal scalability and fail-out resilience. DSR supports a large variety of object schemas including replication and erasure codes, static or dynamic striping, algorithmic or explicit target enumeration, and specific target affinity.

DAOS-POSIX (DSP) is the POSIX emulation layer and is one possible top-layer API. This layer provides POSIX namespace encapsulation into a container. It implements basic POSIX semantics on top of DSR objects and leverages the epoch mechanism to support snapshots and guarantee consistency both for POSIX metadata and for application data. DSP implements its own concurrency control mechanism and is not fully POSIX compliant.


- <a href="/doc/storage_model.md">DAOS Storage Model</a>: Describes the DAOS storage model and architecture.
- <a href="/doc/terminology.md">Terminology & Acronyms</a>
- <a href="client/layering.md">DAOS Storage Stack</a>: Presents the storage stack organization and functionality of each layer.
- <a href="vos/README.md">Versioning Object Store</a>
- <a href="bio/README.md">Blob I/O</a>
- <a href="rdb/README.md">Service Replication</a>
- <a href="pool/README.md">Pool Service</a>
- <a href="container/README.md">Container Service</a>
- <a href="object/README.md">Key Array Object</a>
- <a href="placement/README.md">Algorithmic Object Placement</a>
- <a href="rebuild/README.md">Self-healing</a>
- <a href="security/README.md">Security</a>
- <a href="client/dfs/README.md">POSIX File & Direcotory Emulation</a>
- <a href="control/README.md">Control Plane</a>

<a id="52"></a>

## Client/Server Architecture

<a id="5b"></a>
![/doc/graph/Fig_006.png](/doc/graph/Fig_006.png "DAOS Software Architecture")

As represented in the <a href="#5b">figure</a> above, each DAOS layer (except VOS, which is only present on the storage nodes) is composed of both a client-side and a server-side component. For instance, the DSP server module is responsible for metadata locking and epoch distribution. All top-level layers can have a server-side component, if needed.

This approach allows upper layers of the stack to ship complex operations to storage nodes, which can result in multiple low-level DAOS operations.

<a id="53"></a>

### DAOS Common Libraries

Common functionality and infrastructure shared across all DAOS components are provided in external shared libraries. This includes the following features:

- Hash and checksum routines
- Event and event queue support for non-blocking operations
- Logging and debugging infrastructure
- Locking primitives
- Network transport

<a id="54"></a>

### DAOS Client Libraries

DAOS provides a collection of client libraries (one per-layer) that implement the external API exported to application developers. The DAOS client libraries as well as the networking parts do not spawn any internal threads and can be linked directly with the application. It supports the event and event queue interface for non-blocking operations. During execution, a client library can either call into the client library of the lower layer, or use the network transport to send a RPC to its server counterpart, as show in Figure 4 2.


<a id="55"></a>

### DAOS Server and Server Modules

The DAOS server is a persistent service running on all the storage nodes. It manages incoming requests for all targets hosted on the storage node and provides a stackable modular interface to load server-side modules on demand. Initially, only certified server-side modules can be loaded to address security concerns. In the future, untrusted modules can run in a sandbox managed by control groups (i.e. cgroup), allowing a fine-grained control over the resources (memory, CPU cycles, etc.) consumed by the module.

A server module can register handlers for processing RPCs issued by a counterpart client library or other instance of the same module on different servers. The DAOS server provides service threads with core and NUMA affinity to execute those RPC handlers. Service threads rely on event-based processing, which means that each service thread can manage multiple concurrent requests simultaneously.

Each module may call into the external API of the layer below to eventually access the local versioning object store library in charge of data persistency.

<a id="58"></a>

## Network Transport

DAOS relies on a lightweight user-space communication stack based on function shipping, described in the following sections.

<a id="59"></a>

### Mercury Function Shipper

Mercury  is a user-space communication library designed for HPC systems. It provides an asynchronous RPC framework that abstracts a native fabric interface through a Network Abstraction Layer (NAL), guaranteeing portability. Mercury supports both small data transfers for metadata operations and bulk data transfers for actual data. The Mercury interface is generic to allow any function call to be shipped.

To serve as a network transport for DAOS, Mercury will be augmented with the following features:

- Process group support to allow RPCs to be issued to any rank within a process group. This includes node exclusion from a process group.
- Scalable collective communications with reply aggregation
- RAS system integration

The implementation details of the features of the Mercury function shipper are beyond the scope of this document.

<a id="60"></a>

### Multi-layer Function Shipping

Mercury provides a flexible plugin interface that allows shipping any function from a client to any remote storage server. DAOS will rely on this mechanism to allow any layer in the stack to execute code on both clients and storage servers. This means that any layer of the DAOS stack (including DAOS-POSIX) can directly access the function shipping service to register its own handlers. This allows upper layers of the stack to ship complex operations from the client library to the storage module, which can result in multiple low-level DAOS operations. In the client-side API, DAOS-SR provides the ability to list all the targets where a particular SR object is stored. This allows upper layers of the stack to send RPCs for a particular object to the right location.

<a id="61"></a>

### Node Addressing

Nodes within a Mercury process group are addressed by endpoints that are assigned for the lifetime of the session. DAOS, on the other hand, must store persistent node identifiers in the pool map. Storage targets will therefore need to convert the persistent node identifiers into runtime addresses when transferring the pool map to other targets and clients. RPCs will then be sent through Mercury by specifying the endpoint of the storage node inside the process group. Likewise, any updates to the pool map will require converting the Mercury endpoint into a persistent node identifier.

<a id="62"></a>

## Persistent Memory based Storage

<a id="63"></a>

### In-Memory Storage

The VOS is designed to use a persistent-memory storage model that takes advantage of byte-granular, sub-�õsecond storage access possible with new NVRAM technology. This enables a disruptive change in performance compared to conventional storage systems for application and system metadata, and small, fragmented and misaligned I/O. Direct access to byte-addressable low-latency storage opens up new horizons where metadata can be scanned in less than a second without bothering with seek time and alignment.

The VOS relies on a log-based architecture using persistent memory primarily to maintain internal persistent metadata indexes. The actual data can be stored either in persistent memory directly or in block-based storage (via SPDK  for instance). Special care will be taken when developing the VOS layer because any software bug could corrupt data structures in persistent memory. The VOS therefore checksums its persistent data structures despite the presence of hardware ECC.

The VOS provides a lightweight I/O stack fully in user space, leveraging the NVML open source libraries developed to support this programming model.

<a id="64"></a>

### Lightweight I/O Stack: NVM Library

NVML  is an open source collection of libraries for using persistent memory, optimized specifically for NVRAM. NVML is actually a collection of six libraries among which the libpmemobj library implements relocatable persistent heaps called NVML pools. This includes memory allocation, transactions, and general facilities for persistent memory programming. Locks can be embedded with PM-resident data structures, which are reinitialized (i.e. unlocked) automatically every time the NVML pool is opened. This property makes sure that all locks are always released when the NVML pool is opened.

Although persistent memory is accessible via direct load/store, updates go through multiple levels of caches including the processor L1/2/3 caches and the NVRAM controller. Durability is guaranteed only after all those caches have been explicitly flushed. The VOS maintains internal data structures in persistent memory that must retain some level of consistency so that operation may be resumed without loss of durable data after an unexpected crash, or power outage. The processing of a request will typically result in several memory allocations and updates that must be applied atomically.

Consequently, a transactional interface must be implemented on top of persistent memory to guarantee internal VOS consistency. It is worth noting that PM transactions are different from the DAOS epoch mechanism. PM transactions are used to guarantee consistency of VOS internal data structures when processing incoming requests, regardless of their epoch number. Transactions over persistent memory can be implemented in many different ways, e.g., undo logs, redo logs, a combination of both, or copy-on-write. NVML provides transactions that are local to one thread (not multi-threaded) and rely on undo logs.

<a id="65"></a>

## Software Compatibility

Interoperability in DAOS is handled via protocol and schema versioning for PM data structures.

<a id="66"></a>

### Protocol Compatibility

Limited protocol interoperability is to be provided by the DAOS storage stack. Version compatibility checks will be performed to verify that:

* All targets in the same pool run the same protocol version.
* Client libraries linked with the application may be up to one protocol version older than the targets.

If a protocol version mismatch is detected among storage targets in the same pool, the entire pool will fail to start up and will report failure to the control API. Similarly, connection from clients running a protocol version incompatible with the targets will return an error.

<a id="67"></a>

### PM Schema Compatibility and Upgrade

The schema of persistent data structures might evolve from time to time to fix bugs, add new optimizations or support new features. To that end, VOS supports schema versioning.

Upgrading the schema version is not done automatically and must be initiated by the administrator. A dedicated upgrade tool will be provided to upgrade the schema version to the latest one. All targets in the same pool must have the same schema version. Version checks are performed at pool initialization time to enforce this constraint.

To limit the validation matrix, each new DAOS release will be published with a list of sup-ported schema versions. To run with the new DAOS release, administrators will then need to upgrade the pools to one of the supported schema version. New target will always be reformatted with the latest version. This versioning schema only applies to data structure stored in persistent memory and not to block storage that only stores data buffers with no metadata.
