
# Versioning Object Store

The Versioning Object Store (VOS) is responsible for providing and maintaining a persistent object store that supports byte-granular access and versioning. It maintains its own metadata in persistent memory and may store data either in persistent memory or on block storage, depending on available storage and performance requirements. It must provide this functionality with minimum overhead so that performance can approach the theoretical performance of the underlying hardware as closely as possible, both with respect to latency and bandwidth. Its internal data structures, in both persistent and non-persistent memory, must also support the highest levels of concurrency so that throughput scales over the cores of modern processor architectures. Finally, and critically, it must validate the integrity of all persisted object data to eliminate the possibility of silent data corruption, both in normal operation and under all possible recoverable failures.

This section provides the details for achieving the aforementioned design goals in building a versioning object store for DAOS-M.

This document contains the following sections:

- <a href="#62">Persistent Memory based Storage</a>
    - <a href="#63">In-Memory Storage</a>
    - <a href="#64">Lightweight I/O Stack: NVM Library</a>
- <a href="#71">VOS Concepts</a>
    - <a href="#711">VOS Indexes</a>
    - <a href="#712">Object Listing</a>
-  <a href="#72">Key Value Stores</a>
    - <a href="#721">Operations Supported with Key Value Store</a>
    - <a href="#723">Key in VOS KV Stores</a>
    - <a href="#724">Internal Data Structures</a>
- <a href="#73">Byte Arrays</a>
- <a href="#74">Document Stores</a>
- <a href="#75">Epoch Based Operations</a>
    - <a href="#751">VOS Discard</a>
    - <a href="#752">VOS Aggregate</a>
    - <a href="#753">VOS Flush</a>
- <a href="#76">VOS over NVM Library</a>
    - <a href="#761">Root Object</a>
- <a href="#77">Layout for Index Tables</a>
- <a href="#78">Transactions and Recovery</a>
    - <a href="#781">Discussions on Transaction Model</a>
- <a href="#79">VOS Checksum Management</a>
- <a href="#80">Metadata Overhead</a>

<a id="58"></a>
## Persistent Memory based Storage

<a id="63"></a>
### In-Memory Storage

The VOS is designed to use a persistent-memory storage model that takes advantage of byte-granular, sub-&#371;second storage access possible with new NVRAM technology. This enables a disruptive change in performance compared to conventional storage systems for application and system metadata, and small, fragmented and misaligned I/O. Direct access to byte-addressable low-latency storage opens up new horizons where metadata can be scanned in less than a second without bothering with seek time and alignment.

The VOS relies on a log-based architecture using persistent memory primarily to maintain internal persistent metadata indexes. The actual data can be stored either in persistent memory directly or in block-based storage (via <a href="https://github.com/spdk/spdk">SPDK</a> for instance). The DAOS service has two tiers of storage: Storage Class Memory (SCM) for byte-granular application data and metadata, and NVMe for bulk application data. Similar to how <a href="http://pmem.io/pmdk/">PMDK</a> is currently used to faciliate access to SCM, the Storage Performance Development Kit (SPDK) is used to provide seamless and efficient access to NVMe SSDs. The current DAOS storage model involves three DAOS server xstreams per core, along with one main DAOS server xstream per core mapped to an NVMe SSD device. DAOS storage allocations can occur on either SCM by using a PMDK pmemobj pool, or on NVMe, using an SPDK blob. All local server metadata will be stored in a per-server pmemobj pool on SCM and will include all current and relevant NVMe device, pool, and xstream mapping information. Please refer to the <a href="../bio/README.md">Blob I/O</a> (BIO) module for more information regarding NVMe, SPDK, and per-server metadata. Special care will be taken when developing the VOS layer because any software bug could corrupt data structures in persistent memory. The VOS therefore checksums its persistent data structures despite the presence of hardware ECC.

The VOS provides a lightweight I/O stack fully in user space, leveraging the PMDK open source libraries developed to support this programming model.

<a id="64"></a>

### Lightweight I/O Stack: NVM Library

PMDK is an open source collection of libraries for using persistent memory, optimized specifically for NVRAM. PMDK is actually a collection of several libraries among which the libpmemobj library implements relocatable persistent heaps called PMDK pools. This includes memory allocation, transactions, and general facilities for persistent memory programming. Locks can be embedded with PM-resident data structures, which are reinitialized (i.e. unlocked) automatically every time the PMDK pool is opened. This property makes sure that all locks are always released when the PMDK pool is opened.

Although persistent memory is accessible via direct load/store, updates go through multiple levels of caches including the processor L1/2/3 caches and the NVRAM controller. Durability is guaranteed only after all those caches have been explicitly flushed. The VOS maintains internal data structures in persistent memory that must retain some level of consistency so that operation may be resumed without loss of durable data after an unexpected crash, or power outage. The processing of a request will typically result in several memory allocations and updates that must be applied atomically.

Consequently, a transactional interface must be implemented on top of persistent memory to guarantee internal VOS consistency. It is worth noting that PM transactions are different from the DAOS epoch mechanism. PM transactions are used to guarantee consistency of VOS internal data structures when processing incoming requests, regardless of their epoch number. Transactions over persistent memory can be implemented in many different ways, e.g., undo logs, redo logs, a combination of both, or copy-on-write. PMDK provides transactions that are local to one thread (not multi-threaded) and rely on undo logs.


<a id="71"></a>
## VOS Concepts

The versioning object store provides object storage local to a storage node by initializing a VOS pool (vpool) as one shard of a DAOS pool. A vpool can hold objects for multiple object address spaces called containers. Each vpool is given a unique UID on creation, which is different from the UID of the DAOS pool. The VOS also maintains and provides a way to extract statistics like total space, available space, and number of objects present in a vpool.

The primary purpose of the VOS is to capture and log object updates in arbitrary time order and integrate these into an ordered history that can be traversed efficiently on demand. This provides a major scalability improvement for parallel I/O by correctly ordering conflicting updates without requiring them to be serialized in time. For example, if two application processes agree how to resolve a conflict on a given update, they may write their updates independently with the assurance that they will be resolved in correct order at the VOS.

The VOS also allows all object updates associated with a given timestamp and process group to be discarded. This functionality ensures that when a DAOS transaction must be aborted, all associated updates are discarded before a snapshot is created. This ensures that distributed updates are atomic i.e. when a commit completes, either all updates have been applied or been discarded.

Finally, the VOS may aggregate the history of objects in order to reclaim space used by inaccessible data and to speed access by simplifying indices. For example, when an array object is punched from 0 to infinity in a given epoch, all data updated after the latest snapshot before this epoch, becomes inaccessible once the container is closed.

Internally, the VOS maintains an index of container UUIDs that references each container stored in a particular pool. The container itself contains three indices. The first is an index of objects used to map object IDs to object metadata efficiently when servicing I/O requests. The second index enumerates all object updates by epoch for efficient discard on abort and epoch aggregation. The third index maps container handle cookies, which identify updates from a process group to object IDs within that container. The container handle cookie is used for identifying object IOs associated with a process group and primarily would be used for aborting a transaction associated with a process group from DAOS-M.

The <a href="#7a">figure</a> below shows interactions between the different indexes used inside a VOS pool. VOS objects are not created explicitly, but are created on first write by creating the object metadata and inserting a reference to it in the owning container's object index. All object updates log the data for each update, which may be a new key value, an array extent or a multilevel KV object. References to these updates are inserted into the object metadata index, the container's epoch index and also the container cookie index. Note that "punch" of an extent of an array object and of a key in a key value object are also logged as zeroed extents and negative entries respectively, rather than causing relevant array extents or key values to be discarded. This ensures that the full version history of objects remain accessible.

<a id="7a"></a>
![../../doc/graph/Fig_008.png](../../doc/graph/Fig_008.png " Interactions between container index, object index, epoch index and cookie within one VOS pool (vpool)")

When performing lookup on a KV object, the object index is traversed to find the index node with the highest epoch number less than or equal to the requested epoch (near-epoch) that matches the key. If a value or negative entry is found, it is returned. Otherwise a "miss" is returned meaning that this key has never been updated in this VOS. This ensures that the most recent value in the epoch history of the KV is returned irrespective of the time-order in which they were integrated, and that all updates after the requested epoch are ignored.

Similarly, when reading an array object, its index is traversed to create a gather descriptor that collects all object extent fragments in the requested extent with the highest epoch number less than or equal to the requested epoch. Entries in the gather descriptor either reference an extent containing data, a punched extent that the requestor can interpret as all zeroes, or a "miss", meaning that this VOS has received no updates in this extent.  Again, this ensures that the most recent data in the epoch history of the array is returned for all offsets in the requested extent, irrespective of the time-order in which they were written, and that all updates after the requested epoch are ignored.

A "miss" is distinct from a punched extent in an array object or a punched KV entry in a KV to support storage tiering at higher layers in the stack. This allows the VOS to be used as a cache since it indicates that the VOS has no history at this extent or key, and therefore data must be obtained from the cache's backing storage tier.

<a id="711"></a>

### VOS Indexes

Design of the object index and epoch index tables are similar to the container index table with the object ID and epoch number as keys respectively. The value of the object index table points to the location of the respective object structure. The value in case of the epoch index points to the object ID updated in that epoch.

Objects can be either a key-value index data structure, byte-array index data structure, or a document store. The type of object is identified from the object ID. A document store supports creation of a KV index with two-levels of keys, wherein the value can be either an atomic value or a byte-array value. Document stores is VOS is discussed in the previous <a href="#74">section</a>.

VOS also maintains an index for container handle cookies. These cookies are created by DAOS-M to uniquely identify I/O from different process groups accessing container objects. The cookie index is shown in the <a href="#7a">figure</a> above. The primary motivation to maintain this information in VOS, is to allow discarding of object IOs for each process group. The cookie index table, helps to search and locate objects modified in a certain epoch by a specific cookie. This index can be a simple hash table where the key to the hash table is the cookie and the value is an epoch index table. This allows to fetch a list of all objects which were updated in an epoch associated with a cookie, which can be used while discarding. A hash table is chosen as the data structure to provide fast O(1) lookups. But if the number of cookies are huge, to scale better the same table can be constructed with a two-level b+ tree where the cookie will be the key in first level whose value points to a tree with {object ID, epoch} as the key and the location of the object as its value. The trees at both levels, in this case, would resemble a B+ tree used in KV, as discussed in the previous <a href="#71">section</a>.

<a id="712"></a>

### Object Listing

VOS provides a way to list all non-empty objects IDs in a container. The object ID (see Section 3.1.3) consists of 192 bits of which bits 8 - 16 identify the type of object. This can allow enumeration of object IDs for a type of object. Interfaces to enumerate byte array and key value objects are shown in the <a href="#7b">figure</a> below. At all times, the object index hash table holds only object IDs that are non-empty, i.e., if an index tree associated with an object is empty (after an aggregation/discard), the object ID and its associated indexes are removed from the object index hash table.

<a id="7b"></a>
![../../doc/graph/Fig_009.png](../../doc/graph/Fig_009.png "Interfaces for enumeration KV and Byte array objects")

The interfaces shown in the <a href="7b">figure</a> above track the next object ID to extract with the help of an iterator. The same iterator routines would be used within VOS for enumeration of containers, all object types including key value object types and byte array objects. One use case that benefits from this object listing is the re-silvering process of DAOS-SR (discussed in Chapter 8). Additional metadata information associated with the object layout optionally can be stored along with the object ID during writes/updates.


<a id="72"></a>

## Key Value Stores

High performance simulations generating large quantities of data require indexing and analysis of data, to achieve good insight. Key Value (KV) stores can play a vital role in simplifying storage of such complex data and allowing efficient processing.

VOS provides a multi-version, concurrent KV store on persistent memory that can grow dynamically and provide quick near-epoch retrieval and enumeration of key values.

Although there is an array of previous work on KV stores, most of them focus on cloud environments and do not provide effective versioning support. Some KV stores ,  provide versioning support, but expect monotonically increasing ordering of versions  and further, do not have the concept of near-epoch retrieval.

VOS must be able to accept insertion of KV pairs at any epoch, and must be able to provide good scalability for concurrent updates and lookups on any key-value object. KV objects must also be able to support any type and size of keys and values.


<a id="721"></a>

### Operations Supported with Key Value Store

VOS supports large keys and values with four types of operations namely, update, lookup, punch, and key enumeration.

The update and punch operations add a new key to a KV store or log a new value of an existing key. Punch logs the special value "punched" effectively a negative entry to record the epoch when the key was deleted. In case that punch(es) and/or update(s) share the same epoch, behavior is decided based on the request from upper layer, typically DAOS-SR. DAOS-SR can differentiate behavior on overwrites depending on the type of replication. In case of client side replication to avoid conflicting writes from multiple client SR can request VOS to throw an error, and in case of server side replication, where conflicting updates are serialized, SR can request VOS to allow overwrites. This is discussed in detail in section 8.4.1. If the upper layer requires VOS to throw an error on an update and/or punch sharing the same epoch, VOS would resolve it by throwing an error. The result would be the same in case of two updates sharing a same epoch, except when values and the container handle cookies are the same for overwritten updates. The change in value can be detected based on the checksum of the value associated with an update.

On the other hand, if the upper layer allows overwrites on the same epoch with the same container handle cookie, update and/or punch take precedence depending the order of arrival. VOS would return an error when two cookies associated with subsequent update and/or punch operations are different. Multiple punch on the same epoch would result in success in all cases other than the case when the cookies for the punch operations on the same epoch are different.

Lookup traverses the KV metadata to determine the state of the given key at the given epoch. If the key is not found at all, a "miss" is returned to indicate that the key is absent from this VOS. Otherwise the value at the near-epoch or greatest epoch less than or equal to the requested epoch is returned. If this is the special "punched" value, it means the key was deleted in the requested epoch. The value here refers to the value in the internal tree-data structure. The key-value record of the KV-object is stored in the tree as value of its node. So in case of punch this value contains a "special" return code/flag to identify the punch operation.

VOS also supports enumeration of keys belonging to a particular epoch. VOS uses iterators that can iterate through the KV store in listing all the keys and their associated values to support this enumeration.


<a id="723"></a>

### Key in VOS KV Stores

VOS KV supports key sizes from small keys to extremely large keys. To provide this level of flexibility VOS hashes the keys with an assumption that with a fast and consistent hash function one can get the same hash-value for the same key. This way the actual key is stored once along with the value, and the hash-value of the key is used in the index structure. A lightweight hash function like xxHash  MurMur64  can be used. To verify hash collisions, the actual key in the KV store must be compared with the actual key being inserted or searched, once the node is located. Although with a good hash function collision is a remote chance, comparison of keys is required for correctness. But the approach of hashing keys still avoids having to compare every huge key in the search path, which would save lot of CPU cycles especially during lookups.

KV stores in VOS allow the user to maintain versions of the different KV pairs in random order. For example, an update can happen in epoch 10 and followed by another update in epoch 5, where HCE is less than 5. To provide this level of flexibility, each key in the KV store must maintain the epoch of update/punch along with the key. The ordering of entries in index trees first happens based on the key, and then based on the epochs. This kind of ordering allows epochs of the same key to land in the same subtree, thereby minimizing search costs. In addition, the key would also contain a 64-bit cookie which identifies the process group owning the container handle cookie making the update. This cookie is saved to track records associated with a certain process group. The primary use case is while discarding records associated with a certain epoch & cookie. Typically used by DAOS-M during aborts.


<a id="724"></a>

### Internal Data Structures

Designing a VOS KV store requires a tree data structure that can grow dynamically and re-main self-balanced. The tree needs to be balanced to ensure that time complexity does not increase with increase in tree size. Tree data structures considered are red-black trees and B+ Trees, the former a binary search tree and the latter an n-ary search tree.

Although red-black trees provide less rigid balancing compared to AVL trees, they compensate by having cheaper rebalancing cost. Red-black trees are more widely used in examples such as the Linux kernel, the java-util library and the C++ standard template library. B+ trees differ from B trees in the fact they do not have data associated with their internal nodes. This can facilitate fitting more keys on a page of memory. In addition, leaf-nodes of a B+ trees are linked; this means doing a full scan would require just one linear pass through all the leaf nodes, which can potentially minimize cache misses to access data in comparison to a B Tree.

To support update and punch as mentioned in the previous section (<a href="#721">Operations Supported with Key Value Stores</a>), an epoch-validity range is set along with the associated key for every update or punch request, which marks the key to be valid from the current epoch until the highest possible epoch. Updates to the same key on a future epoch or past epoch, modifies the end epoch validity of the previous update or punch accordingly. This way only one key has a validity range for any given key-epoch pair lookup while the entire history of updates to the key are recorded. This facilitates nearest-epoch search. Both punch and update have similar keys, except for a simple flag identifying the operation on the queried epoch. Lookups must be able to search a given key in a given epoch and return the associated value. In addition to the epoch-validity range the container handle cookie generated by DAOS-M is also stored along with the key of the tree. This cookie is required to identify behavior in case of overwrites on the same epoch.

A simple example input for crearting a KV store is listed in the <a href="#7c">Table</a> below. Both a B+ Tree based index and a red-black tree based index are shown in the <a href="#7c">Table</a> and <a href="#7d"> figure</a> below, respectively. For explanation purposes, representative keys and values are used in the example.

<a id="7c"></a>
<b>Example VOS KV Store input for Update/Punch</b>

|Key|Value|Epoch|Update (U/P)|
|---|---|---|---|
|Key 1|Value 1|1|U|
|Key 2|Value 2|2|U|
|Key 3|Value 3|4|U|
|Key 4|Value 4|1|U|
|Key 1|NIL|2|P|
|Key 2|Value 5|4|U|
|Key 3|Value 6|1|U|

<a id="7d"></a>

![../../doc/graph/Fig_011.png](../../doc/graph/Fig_011.png "Red Black Tree based KV Store with Multi-Key")

The red black tree, like any traditional binary tree, organizes the keys lesser than the root to the left subtree and keys greater than the root to the right subtree. Value pointers are stored along with the keys in each node. On the other hand, a B+ Tree based index stores keys in ascending order at the leaves, which is where the value is stored. The root nodes and internal nodes (color-coded in blue and maroon accordingly) facilitate locating the appropriate leaf node. Each B+ Tree node has multiple slots, where the number of slots is determined from the order. The nodes can have a maximum of order-1 slots. The container handle cookie must be stored with every key in case of red black trees, but in case of B+ Trees having cookies only in leaf nodes would suffice, since cookies are not used in traversing.

In the <a href="#7e">table</a> below, n is number of entries in tree, m is the number of keys, k is the number of the key, epoch entries between two unique keys.

<b>Comparison of average case computational complexity for index</b>
<a id="7e"></a>

|Operation|Reb-black tree|B+Tree|
|---|---|---|
|Update|O(log2n)|O(log<sub>b</sub>n)|
|Lookup|O(log2n)|O(log<sub>b</sub>n)|
|Delete|O(log2n)|O(log<sub>b</sub>n)|
|Enumeration|ø(m* log<sub>2</sub>(n) + log<sub>2</sub>(n))|O(m * k + log<sub>b</sub> (n))|

Although both these solutions are viable implementations, determining the ideal data structure would depend on the performance of these data structures on persistent memory hardware.

VOS also supports concurrent access to these structures, which mandates that the data structure of choice provide good scalability while there are concurrent updates. Compared to B+ Tree, rebalancing in red-black trees causes more intrusive tree structure change; accordingly, B+ Trees may provide better performance with concurrent accesses. Furthermore, because B+ Tree nodes contain many slots depending on the size of each node, prefetching in cache can potentially be easier. In addition, the sequential computational complexities in the <a href="#7e">Table</a> above show that a B+ Tree-based KV store with a reasonable order, can perform better in comparison to a Red-black tree.

VOS supports enumerating keys valid in a given epoch. VOS provides an iterator-based approach, to extract all the keys and values from a KV object. Primarily, KV indexes are ordered by keys and then by epochs. With each key holding a long history of updates, the size of a tree can be huge. Enumeration with a tree-successors approach can result in an asymptotic complexity of è(m* log (n) + log (n)) with red-black trees, where m is the number of keys valid in the requested epoch. It takes O(log2 (n)) to locate the first element in the tree and O(log2 (n)) to locate a successor. Because m keys need to be retrieved, O( m * log2 (n)) would be the complexity of this enumeration.

In the case of B+-trees, leaf nodes are in ascending order, and enumeration would be to parse the leaf nodes directly. The complexity would be O (m * k + logbn), where m is the number of keys valid in an epoch, k is the number of entries between two different keys in B+ tree leaf nodes, and b is the order for the B+tree. Having k epoch entries between two distinct keys incurs in a complexity of O(m * k). The additional O(logbn) is required to locate first leftmost key in the tree. The generic iterator interfaces as shown in <a href="#7d">Figure</a> above would be used for KV enumeration also.

In addition to enumeration of keys for an object valid in an epoch, VOS also supports enumerating keys of an object modified between two epochs. The epoch index table provides keys updated in each epoch. On aggregating the list of keys associated with each epoch, (by keeping the latest update of the key and discarding the older versions) VOS can generate a list of keys, with their latest epoch. By looking up each key from the list in its associated index data structure, VOS can extract values with an iterator-based approach.




<a id="73"></a>

## Byte Arrays

The second type of object supported by VOS is a byte-array object. Byte-array objects, similar to KV stores, allow multiple versions and must be able to write, read, and punch any part of the byte extent range concurrently. The <a href="#7f">figure</a> below shows a simple example of the extents and epoch arrangement within a byte-array object. In this example, the different lines represent the actual data stored in the respective extents and the color-coding points to different threads writing that extent range.

<a id="7f"></a>

<b>Example of extents and epochs in a byte array object</b>

![../../doc/graph/Fig_012.png](../../doc/graph/Fig_012.png "Example of extents and epochs in a byte array object")

In the <a href="7f">above</a> example, there is significant overlap between different extent ranges. VOS supports nearest-epoch access, which necessitates reading the latest value for any given extent range. For example, in the <a href="#7f">figure</a> above, if there is a read request for extent range 4 - 10 at epoch 10, the resulting read buffer should contain extent 7-10 from epoch 9, extent 5-7 from epoch 8, and extent 4-5 from epoch 1. VOS byte-array objects also support punch over both partial and complete extent ranges.

<a id="7g"></a>

<b>Example Input for Extent Epoch Table</b>

|Extent Range|Epoch |Write (or) Punch|
|---|---|---|
|0 - 100|1|Write|
|300 - 400|2|Write
|400 - 500|3|Write|
|30 - 60|10|Punch|
|500 - 600|8|Write|
|600 - 700|9|Write|

Similar to KV stores using a tree-based data structure, construction of an index structure to hold byte-array objects is possible, wherein the primary difference would be to use the extent range as the key for the byte-array index tree and the value would be the buffer containing the data for that extent range.

VOS maintains epoch-validity for every byte-array index added to the index tree. In addition, VOS also maintains the container handle cookie associated with the byte array write. To handle overlaps during writes, VOS splits the nodes, such that at any point there is only one extent-range for a given epoch-validity range. Consider the index tree constructed with input in the <a href="#7g">Table</a> above, as shown in the <a href="#7h">Figure</a> below, (a) and (b) showing the transition between states of the index tree when insertions with overlap happen. The extent index node {0 -100} gets broken down to three nodes {0 - 30}, {30 - 60}, {60 - 100} with the arrival of a punch for the extent range {30 - 60}. This helps to keep track of multi-version writes to unique extent ranges. In other words, splitting nodes during overlap on updates guarantees that, an extent range in a node does not overlap with any other node in the index tree with the same epoch-validity range.

Since the tree is first ordered with extent-index and followed by epochs, all epoch-validity ranges of a specific extent-index falls in the same subtree. The <a href="#7h">Figure</a> below (b) shows that epoch validity ranges of extent range {30 - 60} are in the same subtree.

Reads to an extent range modified in an epoch can be located directly by comparing the start and end index of the byte-array record requested. When encountered with an overlap of matching extent and epoch validity range, VOS updates the buffer from the client with the associated data. If the requested extent range spans over multiple updates split across multiple nodes, VOS continues searching until the extent range for the request is satisfied. In case a punched fragment exists within the requested extent range, VOS would honor the punch, ignore that fragment of the extent range, and return the buffer to the client with partially filled values.

For a non-fragmented extent range in a byte-array index tree, read complexity is O (log (n)) with red-black trees and O (logb (n)) with B+ Trees. However, in the case of fragmented extent ranges, read complexity would be O(m * O (log (n))) for red-black trees and O(m * O (logb(n))) for B+ Trees, where m is the number of fragments to lookup in the tree for satisfying a read request. Although this example uses RB-trees for explanation but the same approach is practicable with B+ trees.

<a id="7h"></a>

<b>Example RB Tree based extent tree Construction for Input from Table <a href="#7g">above</a>. (a) Shows tree after first three inserts and (b) shows the complete tree with additional inserts during overlap</b>

![../../doc/graph/Fig_013.png](../../doc/graph/Fig_013.png "Example RB Tree based extent tree Construction for Input from Table 5 3. (a) Shows tree after first three inserts and (b) shows the complete tree with additional inserts during overlap")

Byte-array objects also support enumeration of extents in the form of listing the difference between two epochs for a given object. Extracting extents modified between two epochs requires determining the byte-array object IDs updated between requested epochs. Byte-array objects can leverage the epoch index table for this kind of listing. Epochs are keys in the epoch-index table where value is a list of object IDs. VOS can extract the list of extents associated with the requested object ID, modified between the requested epochs. The VOS API returns the list to the caller, by maintaining an anchor, and deletes the list once the anchor reaches the end of the list in order to reclaim space. With the object IDs and extents, the caller can read each extent from the byte-array extent tree with byte array read operations discussed earlier.

The approach considered in this design for byte arrays provides support for arbitrarily aligned extents, and allows overwrites to the same extent range at different epochs efficiently through splitting extent ranges valid in a given epoch range. This allows for point lookup for extents valid in a given epoch range. However, this approach is sensitive to arbitrary extent overlaps. An increased number of arbitrary extent overlaps can induce extensive splitting. The Figure <a href="#7i">below</a> shows an example of extensive splitting in case of an overwrite. In this example, the extent range at the latest epoch does not need to split as it overwrites the entire extent range.

<a id="7i"></a>

<b>Example showing extensive splitting of extent ranges during overwrites</b>

![../../doc/graph/Fig_014.png](../../doc/graph/Fig_014.png "Example showing extensive splitting of extent ranges during overwrites")

By limiting the splitting to scenarios where incoming requests partially overwrite an extent in a lower epoch, the number of fragments could be contained. This optimization can potentially simplify inserting extent ranges. But with this approach, all extents will not be unique and additional augmenting (with upper bound extent interval, upper bound and lower bound epoch interval value) similar to those used in interval trees might be needed to determine the fastest search path in the tree to locate an extent range. Search path in such a tree must be determined not-only based on the start offset but also the end offset, start epoch and end epoch. the figure <a href="#7j">below</a> shows an augmented version of the tree in the previous<a href="#7i"> Figure</a>(b), with an additional write at {0 - 100} at epoch 12. Because the augmented values in subtrees are based on max values of end offsets, end epochs and min value of start epochs of all nodes in a subtree. The augmented values must be propagated up the tree, on every insert and delete. The additional metadata at each node creates a bounding box representation with {start-offset, end-offset, min-epoch, max-epoch} representing a rectangle. Although the representation of each write can be a rectangle, since the tree is ordered by start offset during insertion, two rectangles which are close together in a 2D space can potentially land in the left and right subtrees. It would be better to represent this tree as a bounding box based spatial tree capable of traversing rectangles rather than points.

<a id="7j"></a>

<b>Augmented tree to address excessive splitting of nodes while minimizing splitting for inserts in future epochs</b>

![../../doc/graph/Fig_015.png](../../doc/graph/Fig_015.png "Example of four Storage Nodes, eight DAOS Targets and three DAOS Pools")

R-Trees provide a good way to represent dynamically added bounded rectangles. In the case of an epoch-extent tree, rectangles might not be bounded initially as required by R-Trees, but overwrites create bounded rectangles with splitting. So a variant of an R-Tree that can allow splitting of rectangles dynamically would be required to address this problem.

We propose an Extent - Validity trees (EV-Trees) to address this problem. An EV-Tree inherits all properties of an R-Tree. Like an R-Tree, an EV-Tree is also depth-balanced. Leaf nodes are an array of leaf slots at the same level, each representing one rectangle. The value resides in leaf-nodes alone similar to B+ trees. Non-leaf nodes (or) internal nodes represent bounding box of each leaf-node or an internal node with a pointer pointing to the appropriate node. The root node contains at least two slots unless the root is a leaf node.  In-addition to operations inherited from R-trees, EV-Trees support two additional operations, namely, splitting and trimming of rectangles to eliminate overlapping rectangles at leaves.

<a id="7k"></a>

<b>Rectangles representing extent_range.epoch_validity arranged in 2-D space for an order-4 EV-Tree using input in the table <a href="#7g">above</a></b>

![../../doc/graph/Fig_016.png](../../doc/graph/Fig_016.png "Rectangles representing extent_range.epoch_validity arranged in 2-D space for an order-4 EV-Tree using input in the table")

The figure <a href="7l">below</a> shows the rectangles constructed with splitting and trimming operations of EV-Tree for the example in the previous <a href="#7g">table</a> with an additional write at offset {0 - 100} introduced to consider the case for extensive splitting. The figure <a href="#7k">above</a> shows the EV-Tree construction for the same example. Although it looks similar to R-Tree, unlike R-Trees, EV-Trees makes sure there are no overlapping rectangles in the leaves. This is important to identify unique extents at different epochs and search for extents valid at near-epochs.

<a id="7l"></a>

<b>Tree (order - 4) for the example in Table 6 3 (pictorial representation shown in the figure <a href="#7g">above</a></b>

![../../doc/graph/Fig_017.png](../../doc/graph/Fig_017.png "Rectangles representing extent_range.epoch_validity arranged in 2-D space for an order-4 EV-Tree using input in the table")

Splitting and trimming of rectangles in EV-trees occur only when there is an overlap of rectangles. If the extent ranges are completely overwritten in the same epoch, the associated data/value of the extent range is replaced or an error is returned. The behavior is determined by request from upper layer like in KV store discussed in the previous section (<a href="#721">Operations Supported with Key Value Stores</a>. In scenarios where extent ranges get completely overwritten at a later epoch, updating the epoch validity of the rectangle at the earlier epoch would suffice. The algorithm splits rectangles only when a part of an extent range in an earlier epoch is overwritten in a later epoch, where only updating validity wouldn't suffice.

<a id="7m"></a>

<b>Pictorial representation of Split scenarios of rectangles in EV-Tree</a></b>

![../../doc/graph/Fig_018.png](../../doc/graph/Fig_018.png "Pictorial representation of Split scenarios of rectangles in EV-Tree")

In the split scenarios shown <a href="#7m">above</a>, although there are many cases for overlap, we can summarize the split conditions for all required scenarios with the pseudo code shown <a href="#7n">below</a> (Pseudo Code showing Split Conditions for Rectangle). In all other scenarios of overlap apart from the ones detailed in the pseudo code, updating validity of epochs (or trimming) would suffice. Since at anytime a rectangle is added its bound at epoch infinity (264), two rectangles would not overlap such that one rectangle is within the other.

<a id="7n"></a>

<b>Pseudo Code showing Split Conditions for Rectangle</a></b>

![../../doc/graph/Fig_019.png](../../doc/graph/Fig_019.png "Pseudo Code showing Split Conditions for Rectangle")

Although splitting creates new rectangles, these are created primarily to record unique extent ranges at unique epoch ranges. Also, splitting rectangles does not fragment the associated data/value buffer. A reference counter can be maintained in order to track deleted rectangles associated with a value buffer.  Trimming allows an EV-Tree to keep the epoch-validity ranges of the different extent ranges updated, and does not create additional node slots. The split operation is commutative, (i.e.), irrespective of the order in which the rectangles arrive, the resultant set of split rectangles would remain the same. This is important because VOS data structures are expected to be updated by multiple threads in random order.

Bounding boxes are representative internal nodes that construct the minimal bounding rectangle covering all rectangles in the slots of a leaf node. Each internal node of the EV-Tree like an R-tree has as many slots as the order of the tree. An internal node/slot is added when the number of slots in the leaf node overflows. At this point, the choice of the leaf-node split algorithm is important to ensure there is less overlap between bounding boxes. The EV-tree constructed uses a quadratic split to construct the necessary bounding boxes. The quadratic algorithm finds the distance between two rectangles by calculating the area of individual rectangles and subtracting them from the area of the bounding box, i.e., d = area (bounding box) | area (rect1) | area (rect2). The rectangles with the largest distance value are kept in two different nodes, the other rectangles are placed in the bounding box with the least enlargement, ties are resolved by choosing the box with least area, and if there is still a tie, any box is chosen. This is just one approach to split overflowing leaf-nodes. There is a lot of research available on improving this aspect of R-Trees. Linear split  and splitting based on Hilbert curves  are some of the other options available. The methodology of splitting leaf-nodes is a heuristic and can be adapted, based on how the use cases fair with this data structure.

Inserts in an EV-Tree locate the appropriate leaf-node to insert, by checking for overlap. If multiple bounding boxes overlap, the bounding box with least enlargement is chosen. Further ties are resolved by choosing the bounding box with the least area. During insertion, all overlapping leaf-slots have to be checked for updating epoch-validity. Once the validity is updated, the changes are propagated to the subtree roots and root. At an overlap, there can be up to three splits for a rectangle on an earlier epoch, which translates into a maximum of three inserts for every insert. The maximum cost of each insert can be O (m * (log<sub>b</sub>n)) where m is the number of rectangles that overlap.

Searching an EV-Tree would work similar to R-Tree, where an extent range at an epoch is compared with each bounding box in the internal nodes, starting from the root, to determine if they overlap. All overlapping internal nodes must be pursued, till there are matching internal nodes and leaves. Since extent ranges can span across multiple rectangles, a single search can hit multiple rectangles. In an ideal case (where the entire extent range falls on one rectangle) the read cost is O(log<sub>b</sub>n) where b is the order of the tree. In case that reads can span across multiple rectangles, the read cost would be O(m * (log<sub>b</sub>n)) where m is the number of overlapping rectangles.

EV-Trees guarantee that the leaves don't overlap, but the bounding boxes themselves can overlap when the leaf-node split algorithm is not capable of avoiding this overlap. During such a construction of the EV-Tree, reads falling between borders of overlapping bounding boxes might end up searching all the overlapping bounding boxes. In theory, this can have a worst case complexity of O(n). But this would be a remote case, which would mean that the heuristic for splitting leaf-node is weak and needs to be modified.

For deleting nodes from an EV-Tree, same approach as search can be used to locate nodes, and nodes/slots can be deleted. Once deleted, to coalesce multiple leaf-nodes that have less than order/2 entries, reinsertion is done. EV-tree reinserts are done (instead of merging leaf-nodes as in B+ trees) because on deletion of leaf node/slots, the size of bounding boxes changes, and it is important to make sure the rectangles are organized into minimum bounding boxes without unnecessary overlaps. In VOS, delete is required only during aggregation and discard operations. These operations are discussed in a following section (<a hfer="#75">Epoch Based Operations</a>).


<a id="74"></a>

## Document Stores

In addition to Key-value and Byte-array object, VOS would also offer a document store. The primary motivation of this type of object is to facilitate co-location of custom object metadata and object data within the same object. A document store is used in the construction of the document KV object in DAOS-SR (discussed in section 8.4.6). VOS would support two types of values for document store, one would be an atomic value, which can only be updated either completely or not, and the other would be a byte-array/partial value, where values can be updated partially. The existing internal data structures of KV objects and byte-array objects can be leveraged for designing such an object. The existing KV object design could be used to represent the keys, where each key points to a tree of values (atomic/partial). Depending on the type of value, a B+ tree for atomic value or an EV-tree for a byte-array value can be chosen for the {epoch.value} tree.

<a id="7o"></a>

<b>Layers of a  Document store. Value of Key Tree form separate atomic/partial value trees</b>

![../../doc/graph/Fig_020.png](../../doc/graph/Fig_020.png "Layers of a  Document store. Value of Key Tree form separate atomic/partial value trees")

A simple construction of a document store design is shown in the Figure above. In the case of both types of values, the epoch validity of the value tree must be exported to the attribute key tree and the distribution key tree. With this style of construction there is an inherent benefit for enumeration, as the epoch history of the keys is separated from the size of the parent key tree, reducing the size of the tree to be searched.


<a id="751"></a>

## Epoch Based Operations

Epochs provide a way for modifying VOS objects without destroying the history of updates/writes. Each update consumes memory and discarding unused history can help reclaim unused space. VOS provides methods to compact the history of writes/updates and reclaim space in every storage node. VOS also supports rollback of history incase transactions are aborted.

To compact epochs, VOS allows epochs to be aggregated, i.e., the value/extent-data of the latest epoch of any key is always kept over older epochs. This also ensures that merging history does not cause loss of exclusive updates/writes made to an epoch. To rollback history VOS provides the discard operation.

<a id="7p"></a>
![../../doc/graph/Fig_021.png](../../doc/graph/Fig_021.png "Example of four Storage Nodes, eight DAOS Targets and three DAOS Pools")

Aggregate and discard operations in VOS accept a range of epochs to be aggregated or discarded. Discard accepts an additional container handle cookie parameter. The cookie identifies the container handle for a process group which facilitates to limit discard to a particular process group.  For example, a discard operation for all epochs in range [10, 15] with a cookie c, would discard object ID records associated with the cookie c, modified in epochs 10,11,12,13,14,15. The aggregate operation is carried out with the help of the epoch index present in the VOS, while discard uses the cookie index table.

<a id="751"></a>

### VOS Discard

Discard forcefully removes epochs without aggregation. Use of this operation is necessary only when value/extent-data associated with a {container handle cookie, epoch} pair needs to be discarded. During this operation, VOS looks up all objects associated with each cookie in the requested epoch range from the cookie index table and removes the records directly from the respective object trees by looking at their respective epoch validity. DAOS-M requires discard to service abort requests. Abort operations require discard to be synchronous.

During discard, keys and byte-array rectangles need to be searched for nodes/slots whose end-epoch is (discard_epoch -  1). This means that there was an update before the now discarded epoch and its validity got modified to support near-epoch lookup. This epoch validity of the previous update has to be extended to infinity to ensure future lookups at near-epoch would fetch the last known updated value for the key/extent range.

<a id="752"></a>

### VOS Aggregate

During aggregation, VOS must retain only the latest update to a key/extent-range discarding the others. VOS looks up the lists of object ID from the epoch index table for the requested epoch range, and determines for each entry in these lists, whether to delete them or update their epoch. The epoch index table maintains a list of all object IDs that were updated in an epoch and facilitates looking up object IDs ordered by epochs. Each entry in the list is looked up in their associated index tree. Based on the epoch validity of those entries, the decision is made whether to retain the entries and/or increase their lower bound epoch validity to the highest epoch in the aggregate range, or discard them. Specifically, if the key or extent range is valid for highest epoch in the aggregate range it is retained, if not it is discarded. The epoch index table allows all object IDs associated with an epoch that needs to be aggregated to be fetched with an O(1) lookup. For example, if there are 1 billion objects in VOS and only one object has history in the epoch range to be aggregated, the epoch index table restricts the search to one object ID rather than a billion objects.

Another aspect of aggregation in the case of byte-array objects is, to merge nodes in the index tree that were previously split due to I/O operations in epochs, which are now aggregated. Specifically, if the start epoch for a set of split extent ranges are the same, they can be merged to form one extent range. This optimization further reduces the metadata overhead and eliminates unwanted splits for contiguous extent ranges on the same epoch.

Aggregate can be an expensive operation, but using a different thread in the background for the operation by the caller (typically the DAOS-M server) can help hide the cost. Although this operation does alter the respective individual object tree-indexes, this would not be an issue while using a concurrent tree data structure as discussed in sections 6.2 and 6.3.


<a id="753"></a>

### VOS Flush

VOS also provides a flush operation that checks if all updates/writes and their data in a given epoch are durable in the underlying persistent memory. This operation leverages the PMDK library API to check and flush data to persistent memory. PMDK offers, pmem_is_pmem (addr, len) to check if a given address range \[addr, addr+len] consists of persistent memory. PMDK also provides pmem_persist(addr, len) or pmem_msync(addr, len) functions to flush changes durably to persistent memory. Flush ensures all the metadata entries (index trees, epoch table, and object table) and their associated values/extent-data added/modified in the requested epoch are durable in persistent memory. The metadata entries are persisted with the help of PMDK transactions. These transactions ensure a consistent state at all times. The following section (<a href="#76">VOS over NVM Library</a>) presents a detailed discussion on PMDK transactions. The actual data associated with these metadata transactions can be persisted using pmem_persist on transaction commit. VOS flush can be implemented on top of PMDK transactions, either by waiting for all uncommitted transactions to complete, or by tracking for each epoch the last transaction that modified that epoch and then waiting for that transaction to complete.


<a id="76"></a>

## VOS over NVM Library

VOS accesses persistent memory (pmem) with the help of the Non-volatile memory library (PMDK) . A persistent-memory aware file system exposes persistent memory to the operating system. Unlike traditional page-cache based file systems, the pmem aware file-system provides the shortest-path to the storage (i.e., a non-paged load/store). This allows applications to access the underlying persistent memory directly. Although there is direct access, using malloc() or free() to allocate directly from the pool of persistent memory would not work because these interfaces do not support persistence and this can lead to persistent memory leaks on program failures. Persistent memory stores can potentially go through processor cache, which without explicit flushing, can also lead to persistent memory leaks. In addition, cache-line evictions can lead to dirty data still residing in the memory sub-system, which need explicit flushing to the persistent memory. This necessitates the need for transactional interfaces to guarantee that the objects and data are consistent in the persistent memory in the case of failures or crashes. NVM Library (PMDK) provides ways to persistently perform malloc() and free() directly from the application. The library as shown in the <a href="#7q">figure</a> below resides in user space.

<a id="7q"></a>
<b>Architecture of PMDK on persistent memory using a PM-Aware File System </b>

![../../doc/graph/Fig_022.png](../../doc/graph/Fig_022.png "Architecture of PMDK on persistent memory using a PM-Aware File System")

PMDK builds on the Direct Access (DAX)  changes in Linux. PMDK offers a collection of libraries amongst which libpmemobj, which offers transaction based persistent memory, handling is the one discussed in this section. Typically a DAX device is mounted on a ext4 file system. Libpmemobj provides API to memory-map a persistent memory file to get direct access from the DAX file system. VOS can create and maintain its data structures using a pmem file and libpmemobj, such that it remains consistent on system failures. This section elaborates on creating a VOS pool and its internal data structures using PMDK specifically libpmemobj APIs and constructs.


<a id="761"></a>

### Root Object

The root object in PMDK facilitates locating the superblock of a Versioning object store. The superblock is the starting point of accessing the versioning object store and contains information of the location of containers stored inside the pool, the unique uid of the VOS pool, compatibility flags and a lock for synchronization. The following code in the <a href="#7r">Figure</a> below shows the creation of root object for VOS using PMDK pointers.

<a id="7r"></a>
<b>Code block showing the creation of root object for VOS using PMDK pointers</b>

![../../doc/graph/Fig_023.png](../../doc/graph/Fig_023.png "Code block showing the creation of root object for VOS using PMDK pointers")


Pointers in PMDK contain the offset from the beginning of the pool rather than the virtual address space and a unique ID of the pool. Internally, PMDK uses the pool ID to locate the actual virtual address of the pool stored in a hash table. The actual virtual address of the pointer is determined by looking up the address of the pool, using the pool ID and adding the associated offset to it. PMDK represents persistent pointers as PMEMoid, and the pmemobj_direct(PMEMoid oid) converts it to a virtual memory pointer. Although PMDK provides a way for accessing persistent memory pointers as void\*, operating on such pointers can be error-prone because there is no type associated with the pmem pointers. PMDK addresses this issue with the help of named unions and macros as shown in Figure 6.16. PMDK also provides additional macros D_RW and D_RO to convert typed PMEMoid pointers to direct pointers of their associated types, which are equivalents of using pmemobj_direct on the oids and converting the resultant void* pointers to their respective types.

The code in the <a href="#7s">figures</a> below show the  TOID macros for defining typed pointers in PMDK and the internal representation of List entry macros in PMDK. The PMEMmutex lock provides a pmem-aware lock that is similar to traditional pthread locks, with an additional property of auto re-initialization right after pool open, regardless of the state of lock at pool close. The root object in PMDK is created with the pmemobj_root() API or the macro POBJ_ROOT to return a typed pointer. The root object for VOS created with PMDK pointers is shown in the first <a href="#7s">example</a>.

<a id="7s"></a>

<b>Code block showing TOID macros for defining typed pointers in PMDK</b>

![../../doc/graph/Fig_024.png](../../doc/graph/Fig_024.png "Code block showing TOID macros for defining typed pointers in PMDK")

<a id="7t"></a>

<b>Internal Representation of List Entry Macros in PMDK</b>

![../../doc/graph/Fig_025.png](../../doc/graph/Fig_025.png "Internal Representation of List Entry Macros in PMDK")


<a id="77"></a>

## Layout for Index Tables

<a id="7u"></a>
**Code Block representing the layout of container index table structures with PMDK pointers and ListsCode Block representing the layout of container index table structures with PMDK pointers and Lists**


![../../doc/graph/Fig_026.png](../../doc/graph/Fig_026.png "Code Block representing the layout of container index table structures with PMDK pointers and Lists")

The three main index tables for VOS are the container index table, object index table and epoch index table. VOS uses PMDK TOID pointers as shown in the previous <a href="#761">section</a>, to construct all the three index tables The PMDK library libpmemobj also provides a set of list interfaces to create persistent lists. These persistent lists provide separate chaining for hash buckets in the hash table. Persistent list operations represented with POBJ_LIST_* macros and their internal representation are shown in the <a href="7u">Figure</a> above. Each container created in VOS has its own object index and epoch index tables. The container table is a hash table that hashes the container uuid to a value. The value comprises of the persistent memory pointer to both the object and the epoch index tables, and the lowest and highest epoch of that container.

<a id="7v"></a>
**Code Block representing the layout of object index and epoch index entry**

![../../doc/graph/Fig_027.png](../../doc/graph/Fig_027.png "Code Block representing the layout of object index and epoch index entry")

<a id="7w"></a>
**Code Block Representing the layout for B+ Tree in PMDK**

![../../doc/graph/Fig_028.png](../../doc/graph/Fig_028.png "Code Block representing the layout of object index and epoch index entry")

<a id="7w"></a>
**Layout definition for VOS over PMDK**

![../../doc/graph/Fig_029.png](../../doc/graph/Fig_029.png "Layout definition for VOS over PMDK")

<a id="7x"></a>
**Code block representing the pool creation, pool open and root-object creation**

![../../doc/graph/Fig_030.png](../../doc/graph/Fig_030.png "Code block representing the pool creation, pool open and root-object creation")

The first <a href="#7u">figure</a> shows the detailed layout of the container index table with PMDK pointers and list API. Layouts of object table and epoch table are similar to the container index table shown in this <a href="#7v">figure</a>. Each object table entry would have the object id, PMEM pointer to the tree based index structure (either rb-tree/b+-tree).

An epoch-table entry would be comprised of the epoch number and its respective key. Key in the epoch-table is generic and can take the type as byte-array extents or KV keys, and has been left as a void\* persistent memory pointer for that reason. The following code in the previous<a href="#7w">Figure</a> represents the layout for B+ Tree in PMDK. A similar construction would be necessary for the container handle cookie index table. The code in previous<a href="#7w">Figure</a> presents a layout definition for VOS over PMDK.

In addition to providing persistent memory friendly definitions for all the data structures required to maintain metadata in the VOS pool, PMDK requires a clearly defined layout for the PMDK pool. PMDK provides run-time and compile-time safety with specially-defined macros. previous<a href="#7x">Figure</a> shows layout definition for VOS. Both POBJ_LAYOUT_ROOT and POBJ_LAYOUT_TOID perform a TOID_DECLARE as show in  with an additional type_id argument.

POBJ_LAYOUT_BEGIN starts a counter to assign type IDs consecutively until end. This is useful in verifying whether the current version of the layout matches with the existing objects and their type numbers. PMDK provides a TOID_VALID macro to verify type numbers.

On definition of the layout, VOS initializes the pool using pmemobj_create. The pmemobj_open interface allows us to open the pool for future use. PMDK provides the pmemobj_root or its equivalent macro POBJ_ROOT for defining the root object for the pool. The POBJ_LAYOUT_NAME is just a wrapper to translate the layout string created using POBJ_LAYOUT_BEGIN/END to an internal representation of PMDK.

In the case of persistent memory, the actual data apart from the metadata is also stored in the same pool. VOS allocates the pool to be sufficiently larger to support all objects and their metadata. This choice of design is to ensure effective use of underlying storage.

PMDK supports allocation, resizing and freeing objects from the persistent memory pool in a thread-safe and fail-safe manner with interfaces like pmemobj_alloc and pmemobj_free. These routines are atomic with respect to other threads or any power-failure interruption. In the event of program failure or system crash, on recovery the allocations made are guaranteed to be entirely completed or discarded, leaving the persistent memory heap and internal object containers in a consistent state.

A detailed list of these interfaces is available in the manpage  for libpmemobj. The alloc and free interfaces discussed here are non-transactional. libpmemobj offers transactional interfaces to guarantee consistency at all time. The following <a href="#78">section</a> discusses transactions within PMDK for VOS.


<a id="78"></a>

## Transactions and Recovery

Transactions are required with persistent memory to ensure a consistent state at all times. The following code sample shows transaction flow with the different stages using macros.

<a id="7y"></a>
<b>Transaction flow with the different stages using macros</b>

![../../doc/graph/Fig_036.png](../../doc/graph/Fig_036.png ">Transaction flow with the different stages using macros")

PMDK, specifically libpmemobj, provides a set of transactional interfaces to use persistent memory in a fail-safe manner. Libpmemobj transactions allow enclosing a set of operations between start and commit and ensuring a durable and consistent state of the underlying PMEM on transaction commit. In case of a power failure or system crash, libpmemobj guarantees that all uncommitted changes roll back on restart, thereby restoring the consistent state of memory pool from the time when the transaction started.

<b>B+Tree KV tree creation with PMDK transactions</b>
<a id="7z"></a>
![../../doc/graph/Fig_037.png](../../doc/graph/Fig_037.png ">Transaction flow with the different stages using macros")

The library libpmemobj offers a set of functions to create and manage transactions. Each transaction goes through a series of stages namely, TX_STAGE_NONE, TX_STAGE_WORK, TX_STAGE_ONABORT, TX_STAGE_FINALLY and TX_STAGE_ONCOMMIT.

A set of pmemobj_tx_* functions are also provided to change the different stages. libpmemobj also offers macros to eliminate boilerplate code while creating and managing transactions, for example pmemobj_begin() requires the user to set a jmp_buf with setjmp function every time a new transaction is created; this will be enclosed in a macro TX_BEGIN. This section will use macros provided by libpmemobj to explain VOS use of transactions.

The API libpmemobj represents the complete transaction flow represented with macros as shown in the <a href="#7y">figure</a>
above. The second <a href="#7z">figure</a> shows a simple example of creating a new KV store with B+ tree with the help of PMDK transactions. TX_ZNEW macro creates a typed, zeroed object of size type. libpmemobj also offers several transactional allocations like TX_ALLOC, TX_ZALLOC, and TX_REALLOC among others. The detail list of APIs is available in [1].

The pmemobj_tx_add_range_direct interface used in this example takes a snapshot of a persistent memory block of a given size, located at the address ptr in virtual memory spaces and saves it to undo log. On failure, the library rolls back all changes associated with this memory range. The code example in the <a href="#7aa">figure</a>
below uses B+ Tree based KV store for usage of adding fields to ensure tracking in undo logs for recovery (implementations for bplus_tree_insert_empty_leaf and bplus_tree_insert_into_leaf are not shown for simplicity).

<b>Simple example using B+ Tree based KV store for usage of adding fields to ensure tracking in undo logs for recovery</b>

<a id="7aa"></a>
![../../doc/graph/Fig_038.png](../../doc/graph/Fig_038.png "Simple example using B+ Tree based KV store for usage of adding fields to ensure tracking in undo logs for recovery")

To ensure consistency for all objects modified within a transaction, libpmemobj expects all fields to be explicitly added to the transaction to make it a part of the transaction. This is because the library maintains an undo-log to track objects added to a transaction. This log is persistent and is key to rolling back modifications in case of failure. If a particular object is missing in the undo-log, then libpmemobj does not track modifications made to this object causing a persistent memory leak on failure. The code sample in the <a href="#7aa">Figure</a> above shows a partial implementation for insert into a B+ tree for a KV store. This example is an extension of the layout definition of B+ Trees provided in <a href="#7w">*Layout definition for VOS over PMDK\</a>. *A simple 64-bit integer key and a void* value is considered in this example and it is seen that a transaction is started/insert and whenever a particular object is modified, for example the root node in this case, it has to be added to the transaction beforehand.

<a id="781"></a>

### Discussions on Transaction Model

Although PMDK transactions provide guarantees for data consistency, that does not come without a price. PMDK creates, persists and maintains undo logs to provide necessary guarantees for recovery on failure. However, maintaining undo logs is expensive. VOS can group operations on its internal data structures and perform updates to data structures in batches, in order to minimize the overhead from undo-logs.

In addition, PMDK transactions currently only support single-threaded transactions where threads currently do not cooperate within a single transaction and so there is no contention management or conflict detection, which is generally a characteristic of software transactional memories. This is a potential problem because VOS requires concurrent access to its internal data structures, which can increase the number of undo logs to ensure consistency.

There are two possible directions being considered to address this problem. The PMDK team has proposed one solution, in which every thread has its own copy of the tree that, which would be kept in-sync with the help of an insert-only, lock-free, singly-linked list, to which all memory ranges (only offset and size) are appended.

A second approach is to use copy-on-write, where writes do not change existing nodes, but rather create a new node at a new location for providing updates. Once the update is successful, the tree's pointers point to the new node, rather than the old node, and reclaim space by freeing old nodes. The primary benefit of this approach is that updates happen at a separate location without touching the original tree structure. This facilitates discarding all the latest updates to the tree without causing persistent memory leaks. In this case, discarding updates on failure would mean simply discarding all allocated objects for the new updates, as there was no modification to existing data. Constant allocation and freeing of persistent memory can lead to fragmentation. Jemalloc is a heap manager which uses algorithms and approaches to avoid fragmentation . PMDK uses Jemalloc to manage its allocations through memkind . By using PMDK to handle PMEM allocations, VOS can leverage the defragmentation benefits of jemalloc. In addition, auto-defragmentation tools can also be developed in future to address defragmentation due to copy-on-write style approach, similar to files-systems like btrfs.

<a id="79"></a>

## VOS Checksum Management

One of the guarantees that VOS provides is end-to-end data integrity. Data corruption in VOS can happen while reading or writing data due to a various reasons, including leaks or failures in persistent memory, or during data transmission through the wire. VOS supports data integrity check with checksums.

Client side (HDF5, DAOS-SR, and DAOS-M) of the stack provides checksum for both byte extents and key value stores on writes and updates, respectively.

The VOS API for updates and writes will require checksums as arguments from its upper layer(s). VOS requires checksum for both keys and values in case of KV objects. VOS stores the checksum along with the data.

A Lookup operation on a KV will verify the checksum by computing the checksum for the key and value. If reads in byte-arrays spans over multiple extent ranges, VOS would have to recompute the checksum at the server for each individual extent range for verifying their integrity and return the computed checksum of the entire requested extent range to the client. In case if a read requests a partial byte array extent of an existing extent range, VOS would compute the checksum of the existing extent to verify correctness and then return the requested extent range to the client with its computed checksum. When byte array extents are aggregated, VOS individually re-computes checksum of all extent ranges to be merged to verify correctness, and finally computes and saves the checksum for the merged extent range.

Because checksum computation involves multiple layers of stack to be in-sync, VOS plans to leverage and extend either the mchecksum  library or the Intel Storage acceleration library  (https://01.org/intelî-storage-acceleration-library-open-source-version).

<a id="80"></a>

## Metadata Overhead

VOS has many internal data structure making the amount of metadata to maintain object an important factor. Although it would be challenging to provide the exact amount of metadata associated with creating and maintaining different objects within VOS, with certain assumptions we can get an estimate of how much metadata would be required for certain object kinds. This section provides an estimate of the amount of metadata space required for creating and maintaining a single KV object. The primary purpose of this analysis is to present an idea of the metadata costs associated with creating and maintaining objects in VOS. The numbers presented provide approximations rather than modeling or quantifying the exact metadata overhead consumption.

**Assumptions:**

B+-tree with a tree order 8 is used  for implementing the KV object and all the index tables in a VOS pool. Let us assume that:
<ol>
<li>each node header consumes, 32 bytes</li>
<li>A btree record would consume a total of 56bytes where:
  <ol>
  <li>hkey_size takes 16 bytes (128 bits)</li>
  <li>pmem value pointer consumes 16 bytes</li>
  <li>epochs takes 16 bytes</li>
  <li>key and value size consume 8 bytes each</li></ol>
<li>creating a node would cost = ~576bytes (after adjusting for cache alignment</li>
<li>tree root to hold (order, depth, class, feature) would consume 40bytes. Creating tree root would be an one time cost.</li>
</ol>

**Scenarios**
<ol>
<li>In the best case case when the record position in empty in a leaf, the metadata cost would be:
  <ol>
  <li>16 (KV record pointer) + 16 (hkey_size) + 2 (key_count/node) + 32 (additional metadata) = 64 bytes.</li>
  <li>Additional metadata includes epochs, key and value size.</li>
  </ol>
<li>In the most general or average case, with an assumption that the tree is 50% full with a reasonable depth, the metadata cost can be approximately calculated using
</li>
<li>tree root to hold (order, depth, class, feature) would consume 40bytes. Creating tree root would be an one time cost.
<ol>
  <li>(btr_record_metadata + key_count + depth) * 2 * (1 + 1/8 + 1/64 + )</li>
  <li>(64 + 2) * 2 * (1 + 1/8 + 1/64 + ) = ~151bytes</li>
  <li>Since the order assumed is 8, the 1/8 refers to the point that 1 parent record points to 8 leaf records and so on until root.</li>
  </ol>
<li>Metadata initial setup cost for creating one record in a KV including all indexes associated.
<ol>
  <li>create_cookie_tree + create_object_tree + create_kv_object (with one record) + create_epoch_tree</li>
  <li>Create one tree node for every table/object starting from a empty b-tree</li>
  <li>(btr_tree_node + btree_root) = 4 * (544 + 40) = ~2.36kbytes (after padding for cache alignment)</li>
  </ol>
  <li>Once all indexes are created and individual trees have reasonable depth, we can use the average case to determine the metadata cost
  <ol>
  <li>1 object with a million KV records, with 1000 epoch and cookies would consume:  (1 + 1000000 + 1000 + 1000) * 151 = ~145MB
Which is 145MB/106   =  ~152bytes/record</li>
</ol></li>
</ol>

While using document KV we would have additional cost involved in creating one root-tree node for every level on each insert. To keep the analysis simpler, let us assume one distribution key and many attribute keys. Each attribute key would have a separate value tree.  The initial metadata cost is high but the overall update cost in the best case and the average case would still remain the same as in case of the single level b-tree. This is because, once a value tree for a key is created all updates are added directly to the value tree. And once the different levels of the trees have been initialized, at no point will two levels of the trees would get rebalanced simultaneously.

<ol>
<li>Creating one KV - record (initial setup)
<ol>
<li>create_cookie_tree + create_object_tree + create_kv_object (with one record) + create_epoch_tree</li>
<li>Create one tree node for every table/object</li>
<li>6 * (btr_tree_node + root) + 1 * record = (6 * 616) + 64 = ~3.8kbytes</li>
</ol>
<li>Creating one object with one million keys with overwrites at 1000 epochs with 1000 cookies (average case)
<ol>
<li>initial setup cost + (1000 + 1000 + 1) * btree_average_update_metadata_cost = (1000000 * 616) + (1000 + 1000 + 1) * 151 = ~588MB, which is 588MB/106 = ~616 bytes/record
</ol></li>
</ol>

