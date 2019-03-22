# DAOS Storage Model

In this section, we describe the DAOS storage paradigm including its transaction, security and fault models.
This document contains the following sections:

- <a href="#4.1">Storage Architecture</a>
    -  <a href="#4.1.1">DAOS Target</a>
    - <a href="#4.1.2">DAOS Pool</a>
    - <a href="#4.1.3">DAOS Container</a>
- <a href="#4.2">Transaction Model</a>
    -  <a href="#4.2.1">Epoch & Timestamp Ordering</a>
    - <a href="#4.2.2">Container Snapshot</a>
    - <a href="#4.2.3">Distributed Transactions</a>
    - <a href="#4.2.4">Multi-Version Concurrency Control (MVCC)</a>
    - <a href="#4.2.5">Current Limitations</a>
- <a href="#4.3">Fault Model</a>
    -  <a href="#4.3.1">Hierarchical Fault Domains</a>
    - <a href="#4.3.2">Fault Detection and Diagnosis</a>
    - <a href="#4.3.3">Fault Isolation</a>
    - <a href="#4.3.4">Fault Recovery</a>
- <a href="#4.4">Security Model</a>
    -  <a href="#4.4.1">Authentication</a>
    - <a href="#4.4.2">Authorization</a>

<a id="4.1"></a>

## Storage Architecture
We consider a data center with hundreds of thousands of compute nodes interconnected via a scalable high-performance fabric (i.e. Ethernet, RoCE or Infiniband), where all or a subset of the nodes, called storage nodes, have direct access to byte-addressable storage-class memory (SCM) and, optionally, block-based NVMe storage. The DAOS server is a multi-tenant daemon runing on a Linux instance (i.e. natively on the physical node or in a VM or container) of each storage node and exporting through the network the locally-attached storage. Inside a DAOS server, the storage is statically partitioned across multiple targets to optimize concurrency. To avoid contention, each target has its private storage, own pool of service threads and dedicated network context that can be directly addressed over the fabric independently of the other targets hosted on the same storage node. The number of target exported by a DAOS server instance is configurable and depends on the underlying hardware (i.e. number of SCM modules, CPUs, NVMe SSDs, ...). A target is the unit of fault. All DAOS servers connected to the same fabric are grouped to form a DAOS system, identified by a system name. Membership of the DAOS servers are recorded into the system map that assign an unique integer rank to each server. Two different systems comprise two disjoint sets of servers and do not coordinate with each other.

The <a href="#f4.1">figure </a> below represents the fundamental abstractions of the DAOS storage model.

<a id="f4.1"></a>
![graph/daos_abstractions.png](graph/daos_abstractions.png "DAOS Storage Abstractions")

A DAOS pool is a storage reservation distributed across a collection of targets. The actual space allocated to the pool on each target is called a pool shard. The total space allocated to a pool is decided at creation time and can be expanded over time by resizing all the pool shards (within the limit of the storage capacity dedicated to each target) or by spanning more targets (i.e. adding more pool shards). A pool offers storage virtiualization and is the unit of provisionning and isolation. DAOS pools cannot span across multiple systems.

A pool can host multiple transactional object store called DAOS containers. Each container is a private object address space, which can be modified transactionaly and independently of the other containers stored in the same pool. A container is the unit of snapshot and data management. DAOS objects belonging to a container can be distributed across any target of the pool for both performance and resilience and can be accessed through different APIs to efficiently represent structured, semi-structured and unstructured data.

The table below shows the targeted level of scalability for each DAOS concept.

|DAOS Concept|Order of Magnitude|
|---|---|
|System|10<sup>5</sup> Servers (hundreds of thousands) and 10<sup>2</sup> Pools (hundreds)|
|Server|10<sup>1</sup> Targets (tens)|
|Pool|10<sup>2</sup> Containers (hundreds)|
|Container|10<sup>9</sup> Objects (billions)|

<a id="4.1.1"></a>

### DAOS Target

A target is typically associated with a single-ported SCM module and NVMe SSD attached to a single storage node. Moreover, a target does not implement any internal data protection mechanism against storage media failure. As a result, a target is a single point of failure. A dynamic state is associated with each target and is set to either up and running, or down and not available. A target is the unit of performance. Hardware components associated with the target, such as the backend storage medium, the server, and the network, have limited capability and capacity. Target performance parameters such as bandwidth and latency are exported to upper layers.

<a id="4.1.2"></a>

### DAOS Pool

A pool is identified by a unique UUID and maintains target memberships in a persistent versioned list called the pool map. The membership is definitive and consistent, and membership changes are sequentially numbered. The pool map not only records the list of active targets, it also contains the storage topology under the form of a tree that is used to identify targets sharing common hardware components. For instance, the first level of the tree can represent targets sharing the same motherboard, then the second level can represent all motherboards sharing the same rack and finally the third level can represent all racks in the same cage. This framework effectively represents hierarchical fault domains, which are then used to avoid placing redundant data on targets subject to correlated failures. At any point in time, new targets can be added to the pool map and failed ones can be excluded. Moreover, the pool map is fully versioned, which effectively assigns a unique sequence to each modification of the map, more particularly for failed node removal.

A pool shard is a reservation of persistent memory optionally combined with a pre-allocated space on NVMe storage on a specific target. It has a fixed capacity and fails operations when full. Current space usage can be queried at any time and reports the total amount of bytes used by any data type stored in the pool shard.

Upon target failure and exclusion from the pool map, data redundancy inside the pool is automatically restored online. This self-healing process is known as rebuild. Rebuild progress is recorded regularly in special logs in the pool stored in persistent memory to address cascading failures. When new targets are added, data is automatically migrated to the newly added targets to redistribute space usage equally among all the members. This process is known as space rebalancing and uses dedicated persistent logs as well to support interruption and restart.
A pool is a set of targets spread across different storage nodes over which data and metadata are distributed to achieve horizontal scalability, and replicated or erasure-coded to ensure durability and availability.

When creating a pool, a set of system properties must be defined to configure the different features supported by the pool. In addition, user can defined their own attributes that will be stored persistently.

A pool is only accessible to authenticated and authorized applications. Multiple security frameworks could be supported, from NFSv4 access control lists to third party-based authentication (such as Kerberos). Security is enforced when connecting to the pool. Upon successful connection to the pool, a connection context is returned to the application process.

As detailed previously, a pool stores many different sorts of persistent metadata, such as the pool map, authentication and authorization information, user attributes, properties and rebuild logs. Such metadata are critical and require the highest level of resiliency. Therefore, the pool metadata are replicated on a few nodes from distinct high-level fault domains. For very large configurations with hundreds of thousands of storage nodes, only a very small fraction of those nodes (in the order of tens) run the pool metadata service. With a limited number of storage nodes, DAOS can afford to rely on a consensus algorithm to reach agreement and to guarantee consistency in the presence of faults and to avoid split-brain syndrome.

<a id="4.1.3"></a>

### DAOS Container

A container represents an object address space inside a pool and is identified by a UUID. The diagram below represents how the top-level API (i.e. I/O middleware, domain-specific data format, big data or AI frameworks ...) could use the container concept to store related datasets.

![graph/containers.png](graph/containers.png "DAOS Container Example")

Likewise to pools, containers can store user attributes and a set of properties must be passed at container creation time to configure different features like checksums.

To access a container, an application must first connect to the pool and then open the container. If the application is authorized to access the container, a container handle is returned. This includes capabilities that authorize any process in the application to access the container and its contents. The opening process may share this handle with any or all of its peers. Their capabilities are revoked either on container close.

Objects in a container may have different schemas for data distribution and redundancy over targets. Dynamic or static striping, replication or erasure code are as many parameters required to define the object schema. The object class defines common schema attributes for a set of objects. Each object class is assigned a unique identifier and is associated with a given schema at the pool level. A new object class can be defined at any time with a configurable schema, which is then immutable after creation, or at least until all objects belonging to the class have been destroyed. For convenience, several object classes expected to be the most commonly used will be predefined by default when the pool is created, as shown the <a href="#t4.2">table</a> below.

<a id="t4.2"></a>
**Sample of Pre-defined Object Classes**

| Object Class (RW = read/write, RM = read-mostly|Redundancy|Layout (SC = stripe count, RC = replica count, PC = parity count, TGT = target|
|---|---|---|
|Small size & RW	|Replication	|static SCxRC, e.g. 1x4|
|Small size & RM	|Erasure code	|static SC+PC, e.g. 4+2|
|Large size & RW	|Replication	|static SCxRC over max #targets)|
|Large size & RM	|Erasure code	|static SCx(SC+PC) w/ max #TGT)|
|Unknown size & RW	|Replication	|SCxRC, e.g. 1x4 initially and grows|
|Unknown size & RM	|Erasure code	|SC+PC, e.g. 4+2 initially and grows|

As shown below, each object is identified in the container by a unique 128-bit object address. The high 32 bits of the object address are reserved for DAOS to encode internal metadata such as the object class. The remaing 96 bits are managed by the API user and should be unique inside the container. Those bits can be used by upper layers of the stack to encode their own metadata as long as unicity is guaranteed. A per-container 64-bit scalable object ID allocator is provided in the DAOS API. The object ID to be stored by the application is the full 128-bit address which is for single use only and can be associated with only a single object schema.

**DAOS Object ID Structure**
<pre>
<---------------------------------- 128 bits ---------------------------------->
--------------------------------------------------------------------------------
|DAOS Internal Bits|                Unique User Bits                           |
--------------------------------------------------------------------------------
<---- 32 bits ----><------------------------- 96 bits ------------------------->
</pre>

A container is the basic unit of transaction and versioning. All object operations are implicitely tagged by the DAOS library with a timestamp called an epoch. The DAOS transaction API allows to combine multiple object updates into a single atomic transaction with multi-version concurrency control based on epoch ordering.
All the versioned updates may periodically be aggregated to reclaim space utilized by overlapping writes and to reduce metadata complexity. A snapshot is a permanent reference that can be placed on a specific epoch to prevent aggregation.

Container metadata (i.e. list of snapshots, container open handles, object class, user attributes, properties, etc.) are stored in persistent memory and maintained by a dedicated container metadata service that either uses the same replicated engine as the parent metadata pool service, or has its own engine. This is configurable when creating a container.

<a id="4.1.4"></a>

### DAOS Object

To avoid scaling problems and overhead common to traditional storage system, DAOS objects are intentionally very simple. No default object metadata beyond the type and schema are provided. This means that the system does not maintain time, size, owner, permissions or even track openers. To achieve high availability and horizontal scalability, many object schemas (replication/erasure code, static/dynamic striping, etc.) are provided. The schema framework is flexible and easily expandable to allow for new custom schema types in the future. The layout is generated algorithmically on object open from the object identifier and the pool map. End-to-end integrity is assured by protecting object data with checksums during network transfer and storage.

A DAOS object can be accessed through different APIs:
- <b>Multi-level key-array API</b> is the native object interface with locality feature. The key is split into a distribution (i.e. dkey) and an attribute (i.e. akey) keys. Both the dkey and akey can be of variables length and of any types (ie. a string, an integer or even a complex data structure). All entries under the same dkey are guaranteed to be collocated on the same target. The value associated with akey can be either a single variable-length value that cannot be partially overwritten or an array of fixed-length values. Both the akeys and dkeys support enumeration.
- <b>Key-value API</b> provides simple key and variable-length value interface. It supports the traditional put, get, remove and list operations.
- <b>Array API</b> implements one-dimensional array of fixed-size elements addressed by a 64-bit offset. A DAOS array supports arbitrary extent read, write and punch operations.

<a id="4.2"></a>

## Transaction Model (TODO)

The primary goal of the DAOS transaction model is to provide a high degree of concurrency and control over durability of the application data and metadata. Applications should be able to safely update the dataset in-place and rollback to a known, consistent state on failure.

Each DAOS I/O operation is tagged with a timestamp. Distributed serializable transactions are exported through the DAOS API and can be used to guarantee consistency for parts of the datasets that are concurrently accessed. Container snapshots allow to create complex workflow pipeline.

<a id="4.2.1"></a>
### Epoch & Timestamp Ordering

Each DAOS I/O operation is tagged with a timestamp called epoch. An epoch is a 64-bit integer that integrates both a logical and physical clocks. The DAOS API provides helper functions to convert an epoch to traditional POSIX time (i.e. struct timespec, see clock_gettime(3)).

<a id="4.2.2"></a>
### Container Snapshot

As shown in the <a href="#f4.4">figure</a> below below, the state of a container evolves as a sequence of consistent snapshots. Each state transition is sequentially numbered by epoch.

<a id="f4.4"></a>
**Example of Container Snapshots**
![graph/container_snapshots.png](graph/container_snapshots.png "Example of Container Snapshots")

Epochs are arranged in order so that epochs less than or equal to the containerâs highest committed epoch (HCE) correspond to immutable, globally-consistent container versions. The current container HCE may be queried at any time and consistent distributed reads on a single container are achieved by reading from the same committed epoch. In a producer/consumer workflow, consumer applications may also wait for a new container HCE so that updates can be processed as the producers commit them. The immutability of the containerâs HCE guarantees that the consumer sees consistent data, even while the producers continue with new updates. Unaccessible epochs for a container may be aggregated from time to time to reclaim space utilized by overlapping writes and reduce metadata complexity. A named snapshot is a permanent reference that can be placed on a committed epoch to prevent this aggregation. Some statistics (like how much extra storage is consumed) are exported for each snapshot.

On the other hand, epochs greater than the container HCE denotes non-globally committed transactions where new writes are taking place. Those epochs are guaranteed to be applied to the container in epoch order, regardless of execution order. This allows an application not only to describe transactions in which multiple distributed processes update multiple distributed objects, but also allows it to execute any such transaction concurrently.

On successful commit of all the producers, the specified epoch becomes the new container HCE. Uncommitted epochs are also readable, but without any consistency guarantee.

Container snapshot and versioning allows to support native producer/consumer pipeline as represented in the diagram below.

![graph/producer_consumer.png](graph/producer_consumer.png "Producer/Consumer Workflow with DAOS Containers")

The producer will generate a snapshot once a consistent version of the datatsets has been successfully written. A simple publish-subscribe API allows the consumer to be notified of the new snapshot. Both the producer and consumer then operate on different version of the container and don't need to be serialized. Once the producer generates a new version of the datasets, the consumder may just query the differences between the two snapshots and process only the incremental updates.

Access to a container is controlled by the container handle. To acquire a valid handle, an application process must open the container and pass the security checks (user/group permission, â¦). The opening process may then share this handle (via local2global() / global2local()) with any or all of its peers (similar to the openg() POSIX extension). A container handle is then revoked, either on explicit container close or on request from the system resource manager. All container handles along with the epoch state are stored persistently in the container metadata.

A set of processes sharing the same container handle is called a process group.  One process may belong to multiple process groups corresponding to one or more containers.  A container can be opened by multiple process groups at the same time, regardless of their open mode.

All members of a process group effectively participate in the same transaction scope. This means that all updates submitted by a process group are committed or rolled back as one. This âall or nothingâ semantic eliminates the possibility of partially-integrated updates after failure.

<b>Epoch Hold and Handle HCE</b>

To modify a container, a process group must first declare its intent to change the container by obtaining an epoch hold. This operation must be performed by a single member of the process group and can take a minimal held epoch number as input parameter. On successful execution, the handle lowest held epoch (i.e., handle LHE) is returned to the caller. The handle LHE is guaranteed to be higher than the current container HCE and the specified epoch. The process group is now expected to commit every epoch greater than or equal to the handle LHE.
Epoch commit is performed by any member of a process group, to atomically make all updates submitted by the process group, in all epochs up to the commit epoch, durable. By committing, the process group guarantees that all updates in the transaction have been applied to its satisfaction. On successful commit, the handle HCE is increased to the committed epoch and the handle LHE to the handle HCE + 1. It is the responsibility of the programming model, library or user to determine if all members of the process group have contributed changes prior to commit.
All operations submitted by a process group tagged with an epoch smaller or equal to the handle HCE are guaranteed to be applied and durable. On the other hand, operations tagged with epoch greater than the handle HCE are automatically rolled back on failure.
A hold can be released at any time, which causes the open handle to become quiescent. On close, the hold is automatically released, if it is not already. This causes uncommitted updates submitted by this process group to all future epochs to be discarded. A member of the process group can also invoke the discard operation directly, to discard all its updates submitted in a given range of uncommitted epochs.

<b>Epoch Reference and Handle LRE</b>

On open, any container handle is granted a default read reference on the current container HCE. This reference, called the handle lowest referenced epoch (i.e., handle LRE), guarantees that the current container HCE, as well as any future globally-committed epochs, remain readable and thus cannot be aggregated. The handle LRE can be moved forward to a newer globally-committed epoch through an explicit call to epoch slip.
At the container level, the container metadata tracks the container LRE that is equal to the smallest handle LRE across all container open handles. Epoch aggregation is triggered each time the container LRE is moved forward. When all container handles are closed, the container LRE is equal to the container HCE, which is then the only available unnamed container version. As mentioned before, a named snapshot is a persistent reference on a single epoch, and isnât associated with any container handle. A named snapshot is guaranteed to be readable until it is explicitly destroyed.

<a id="4.2.3"></a>
### Distributed Transactions

Prior to epoch commit, all I/O operations submitted by a process group against the to-be-committed epoch must be flushed. This assures that all caches are properly drained and updates are stored persistently. As a summary, the typical flow of a DAOS transaction is the following:

1.	Open the container
2.	Obtain an epoch hold
3.	Submit I/O operations against the lowest held epoch
4.	Flush all the operations
5.	Commit the lowest held epoch which is increased by 1
6.	Goto 3 if more updates must be submitted
7.	Release the epoch hold
8.	Close the container

I/O operations submitted with different epoch numbers from the same or different process groups are guaranteed to be applied in epoch order, regardless of execution order. Concurrency control mechanism that might be implemented on top of DAOS are strongly encouraged to serialize conflicting updates by using different epoch numbers in order to guarantee proper ordering. Therefore, conflicting I/O operations submitted by two different container handles to the same epoch is considered a programmatic error and will fail at I/O execution time.

As for conflicting I/O operations inside the same epoch submitted with the same container handle, the only guarantee is that an I/O started after the successful completion of another one wonât be reordered. This means that concurrent overlapping I/O operations are not guaranteed to be properly serialized and will generate non-deterministic results. This can be particularly harmful when overwriting the same extent of a replicated object because replicas could order I/Os differently and eventually become inconsistent. To address this problem, overwrite in the same epoch will only be supported with server-side replication, where concurrent writes can be properly ordered, whereas overwrite will generate errors with client-side synchronous replication. More details on this are provided in the MAKEREF DAOS-SR REPLICATIONsection 8.4.1.

<a id="4.2.4"></a>
### Multi-Version Concurrency Control (MVCC)

While DAOS epochs can be used to support Atomicity, Consistency and Durability guarantees, the Isolation property is considered beyond the scope of the DAOS transaction model. No mechanism to detect and resolve conflicts among different transactions as well as within a transaction is provided or imposed. The top-level API is thus responsible for implementing its own concurrency control strategy (e.g., two-phase locking, timestamp ordering, etc.) depending on its own needs. DAOS provides some functionality to facilitate the development of conflict detection and resolution mechanisms on top of its transaction model:

* Middleware layered over DAOS can execute code on the DAOS target. This provides a way to run the concurrency control mechanism where the data is located. More details on this mechanism are provided in sections 4.2.3 and 4.3.2.
* Changes submitted against any epoch can be enumerated, provided that the epoch has not been aggregated. This allows the development of an optimistic approach where conflict detection is delayed until its end, without blocking any operations. This server-side API indeed allows to iterate over the metadata tree to list all the operations submitted against a given epoch for any objects. The transaction can then be aborted if it does not meet the serializability or recoverability rules.

<a id="4.2.5"></a>
### Current Limitations

The DAOS transaction model supports atomic updates across different containers from the same pool. This covers all server nodes of the pool. Cross-container transactions are achieved by committing epochs from different containers altogether by using a two-phase commit approach (see 7.8.3). Updates must be submitted independently to each container in a specific epoch. Once each individual epoch has flushed, a single commit request with a list of container handles and respective epochs can be issued. Upon success, each container has successfully committed its respective epoch and each handle HCE is updated. On failure, none of the epochs are committed. Chapter 7 provides further details on cross-container transactions.

<a id="4.3"></a>
## Fault Model (TODO)

DAOS relies on massively distributed single-ported storage. Each target is thus effectively treated as a single point of failure. DAOS achieves availability and durability of both data and metadata by providing redundancy across targets in different fault domains. DAOS internal pool and container metadata are replicated via a consensus algorithm. DAOS objects are then safely replicated or erasure-coded leveraging the internal DAOS distributed transaction mechanisms. The purpose of this section is to provide details on how DAOS achieves fault tolerance and guarantees object resilience.

<a id="4.3.1"></a>
### Hierarchical Fault Domains

A fault domain is a set of servers sharing the same point of failure and which are thus likely to fail altogether. DAOS assumes that fault domains are hierarchical and do no overlap. The actual hierarchy and fault domain membership must be supplied by an external database used by DAOS to generate the pool map.

Pool metadata are replicated on several nodes from different high-level fault domains for high availability, whereas object data is replicated or erasure-coded over a variable number of fault domains depending on the selected object class.

<a id="4.3.2"></a>
### Fault Detection

TODO: GOSSIP

DAOS delegates authority for detection of storage node failure to an external RAS service, which delivers authoritative notifications. The RAS system (Reliability, Availability, and Serviceability) is different from traditional cluster monitoring tools. It receives diagnosis inputs from multiple sources: baseboard management controllers (BMC), fabric, distributed software running on the cluster, etc. For instance, DAOS clients experiencing RPC timeout should be able to report those problems to the RAS service. All this data is carefully analyzed and correlated by the RAS system that then makes authoritative and unilateral decision on node eviction.

The RAS system then delivers eviction notifications reliably (i.e., resend until acknowledged) and in consistent order to all the processes registered to receive them. All the storage nodes running the pool metadata service typically subscribe to this notification service. This ensures that no notifications are missed, even in the case of service leader change.

The RAS system can use a consensus algorithm to assure the durability and availability of this service. Internals of the RAS system are beyond the scope of this document.

<a id="4.3.3"></a>
### Fault Isolation

Once the RAS system has notified the pool metadata service of a failure, the faulty node must be excluded from the pool map. This is done automatically and silently if the DAOS pool has enough internal redundancy to cope with the failure. If data has been lost, I/O errors will be returned to the applications on access. The data loss will also be reported to the administrator. Upon exclusion, the new version of the pool map is eagerly pushed to all storage targets. At this point, the pool enters a degraded mode that might require extra processing on access (e.g. reconstructing data out of erasure code). Consequently, DAOS client and storage nodes retry RPC indefinitely until they are notified of target exclusion from the pool map. At this point, all outstanding communications with the evicted target are aborted and no further messages should be sent to the target until it is explicitly reintegrated (possibly only after maintenance action).

All storage targets are promptly notified of pool map changes by the pool metadata service. This is not the case for client nodes, which are lazily informed of pool map invalidation each time they communicate with servers. To do so, clients pack in every RPC their current pool map version. Servers reply not only with the current pool map version if different, but also with the actual changes between the client and server version if the log size is reasonable. Consequently, when a DAOS client experiences RPC timeout, it regularly communicates with the other DAOS target to guarantee that its pool map is always current. Clients will then eventually be informed of the target exclusion and enter into degraded mode.

This mechanism guarantees global node eviction and that all nodes eventually share the same view of target aliveness.

Moreover, it is worth nothing that the poolmap is the authority. While the pool service listens to RAS notifications and excludes nodes accordingly, the DAOS pool service can also decide by itself to exclude nodes from the poolmap without involving RAS. This can happen if a node is found to be regularly unresponsive or misbehaving, or if the administrator asks for this node to be evicted from the poolmap.

<a id="4.3.4"></a>
### Fault Recovery

Upon exclusion from the pool map, each target starts the resilvering process automatically to restore data redundancy. First, each target creates a list of local objects impacted by the target exclusion. This is done by scanning a local object table maintained by the underlying storage layer. Then for each impacted object, the location of the new object shard is determined and redundancy of the object restored for the whole history (i.e., committed epochs) as well as for future transactions (i.e., uncommitted epochs). Once all impacted objects have been rebuilt, the pool map is updated a second time to report the target as failed out. This marks the end of resilvering and the exit from degraded mode for this particular fault. At this point, the system has fully recovered from the fault and client nodes can now read from the rebuilt object shards. A future optimization could be to allow each object to exit independently from degraded mode without waiting for all the impacted objects to be recovered. This could be achieved by allowing reads from the object shard being rebuilt and returning cache miss when no valid data is found. On cache miss, the client would then try to read again from another replica or reconstruct data from erasure code.

To deal with resilvering interruption and cascading failure, persistent local resilvering logs are maintained. Each target maintains locally a more fine-grained log recording the list of objects to be resilvered, what objects have been resilvered already, and which ones are in progress (potentially at which epoch and offset/hash).

This resilvering process is executed online while applications continue accessing and updating objects.

<a id="4.4"></a>
## Security Model [TODO]

DAOS supports a subset of the NFSv4 ACLs for both pools and containers.
