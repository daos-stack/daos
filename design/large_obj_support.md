# Summary

-   **Problem**: Currently one object can have only one E (evict-able) bucket. Consequently, large object spillover into NE (Non-evict-able) buckets when the associated E bucket is exhausted. This leads to premature NE bucket depletion and ENOSPC errors, even while significant capacity remains available in the E-buckets zones.

-   **Proposal**: Objects must be allowed to span multiple E buckets. Simultaneously, partial on-demand object loading must be implemented to support giant objects.

-   **Impact**: This prevents premature ENOSPC, as large objects will no longer excessively consume NE buckets.

# Background

## Current Architecture

In MD-on-SSD Phase 2, the metadata blob on SSD can exceed the in-memory VOS pool capacity. This allows DAOS to support larger metadata capacities despite limited DRAM. Because the entire metadata footprint cannot fit into memory simultaneously, a standard cache eviction mechanism dynamically swaps out cold data and loads requested metadata during I/O handling.

Both the metadata blob on SSD and the VOS pool in memory is divided into 16 MB buckets, which serve as the unit of granularity for metadata swapping. In-memory VOS pools consist of two bucket types:

-   **Evictable Buckets (E-buckets):** Store object-specific metadata that can be evicted to SSD when cold.

-   **Non-Evictable Buckets (NE-buckets):** Store shared metadata that must remain pinned in memory (e.g., container or DTX metadata). NE-buckets can also accept spill-over object data if needed.

> An object is associated with exactly one E-bucket, recorded in its durable format. When its assigned E-bucket fills up, new allocations for that object spill over into NE-buckets, which can trigger the premature ENOSPC issue described in the summary.

## Scaling Limit

-   Bucket size is 16MB

-   Average bucket load time is around 4 milli-second

-   Each bucket can accommodate up to 157k array records, which means single E-bucket can hold:

    -   620MB file with 4k io size

    -   19GB file with 128k io size

    -   157GB file with 1MB io size

-   Each bucket can accommodate between 25k and 50k KV pairs, depending on the key size and object type:

    -   25k KV pairs with 128 bytes key, value on data blob (value size >= 4k), object type is DAOS_OT_MULTI_HASHED

    -   33k KV pairs with 128 bytes key, 128 bytes value, object type is DAOS_OT_KV_HASHED (flat hashed dkey, no akey)

    -   37k KV pairs with 128 bytes key, value on data blob, object type is DAOS_OT_KV_HASHED

    -   54k KV pairs with integer key, value on data blob, object type is DAOS_OT_KV_UINT64

# Goals and Non-Goals

## Goals

-   **Premature ENOSPC Elimination**: Prevent false ENOSPC errors caused by large objects whose metadata exceeds a single bucket\'s capacity.

-   **Partial Object Loading**: Load only the minimal metadata required before starting a transaction, rather than the entire object. This enables support for giant objects that exceed available memory buckets.

-   **Eviction Thrashing Prevention**: Avoid repetitive eviction and reloading of the same bucket during traversal operations.

## Non-Goals

-   **Defragmentation**: A background service that consolidates object metadata from sparse buckets into fewer, denser buckets. Detailed design will be provided in a separate document and deferred to a future release.

-   **Anti-fragmentation**: Allocator-level optimizations designed to minimize initial object metadata fragmentation. Detailed design will be covered in a separate document.

# Design

## Refactor VOS to Support Multiple E Buckets per Object

### Durable Format Changes

Current durable format of VOS object and GC item:

#define VOS_OBJ_BKTS_MAX 4
#define VOS_GC_BKTS_MAX  2

struct vos_obj_pd_df {
    struct vos_obj_df p2_obj_df;
    uint32_t          p2_bkt_ids[VOS_OBJ_BKTS_MAX];
    uint64_t          p2_reserved;
};

struct vos_gc_item {
    umem_off_t it_addr;
    uint32_t   it_bkt_ids[VOS_GC_BKTS_MAX];
};

The fixed size *p2_bkt_ids\[VOS_OBJ_BKTS_MAX\]* array can't hold an arbitrary number of bucket IDs. Replace it with a compact linked structure stored in the NE zone so that the list itself is always in memory after pool loaded:

#define VOS_OBJ_BKT_NODE_CAP 15 /* fits in a 128-byte slab */

struct vos_obj_bkt_node_df {
    uint32_t   bn_bkt_ids[VOS_OBJ_BKT_NODE_CAP];
    uint8_t    bn_bkt_cnt;
    uint8_t    bn_pad[3];
    umem_off_t bn_next; /* next node or NULL */
};

struct vos_obj_p2_df {
    struct vos_obj_df p2_obj_df;
    uint32_t          p2_bkt_id0;   /* first bucket ID */
    uint32_t          p2_bkt_cnt;   /* total bucket count */
    umem_off_t        p2_bkt_extra; /* head of overflow node chain */
    uint64_t          p2_reserved;
};

-   **p2_bkt_id0**: Holds the first (and usually only) bucket ID for fast-path access.

-   **p2_bkt_extra**: Points to a singly linked list of *vos_obj_bkt_node_df* nodes allocated in the NE zone. Each node holds up to 15 bucket IDs, giving 15 \* N additional E buckets per extra node.

-   **p2_bkt_cnt**: Caches the total count so callers never need to traverse the chain for a count.

This format change requires a new *POOL_DF_VERSION* bump.

*vos_gc_item.it_bkt_ids\[\]* is sized *by VOS_GC_BKTS_MAX* and stored in packed GC bags. Extending this field to hold an arbitrary number of IDs would inflate every GC item. Instead, the GC item stores the offset of the *vos_obj_p2_df* and reads bucket IDs from the durable chain at drain time.

The partial object loading feature introduced in the following section constrains each dkey to a single E bucket. Consequently, *vos_gc_item.it_bkt_ids\[\]* will be used to store the E bucket ID for both dkey and akey. This ensures the *vos_obj_p2_df* can be safely freed once the object tree is flattened during GC.

### Object Allocation

In the current implementation, an object's initial allocation uses *umem_allot_mb_evictable()* to provision its first E bucket for the object. Subsequent allocations for the same object draw space from this assigned bucket using *umem_alloc_from_bucket()*. If the bucket lacks sufficient space to satisfy a request, the allocation automatically spills over to the NE zone.

In a multi-E-bucket-per-object scenario, the initial allocation logic remains unchanged. Subsequent allocations utilize the new *umem_alloc_from_buckets()* or *umem_reserve_from_buckets()* function to allocate/reserve from the object's assigned buckets. If these buckets lack sufficient space to satisfy a request, the allocation spills over to a new SOE bucket, and its IDs are appended to the bucket list in vos_obj_df. If the object continues to grow and the number of E buckets reaches a predefined limit, subsequent allocations will automatically spill over to the NE zone.

### Object Pin

While the object pin and unpin framework remain unchanged, the key difference is that any newly appended SOE (See: Spillover Evict Bucket) bucket must now be explicitly held by the object until it is released.

## Partial Object Loading

In the current md-on-ssd phase2 implementation, an entire object must be loaded and pinned in memory before a transaction initiates. Consequently, a single dkey update forces the system to load many unnecessary buckets for multi-E-buckets objects. This not only increases cache pressure unnecessarily but also caps the maximum object size to the amount of available memory buckets.

Proposed optimization:

-   **Granular tracking:** Track the specific bucket ID associated with each individual dkey.

-   **Partial loading/pinning:** Pin only the necessary partial buckets prior to transaction initiation, minimizing the total number of pinned buckets for multi-E-buckets objects.

### Dkey Bucket Tracking

The bucket ID associated with each dkey will be stored in the *vos_krec_df* along with the dkey checksum. A bit flag in *kr_bmap* will indicate whether the bucket ID is present.

### Dkey Allocation

When an object has only one E bucket, the allocation strategy remains unchanged: all allocations go into that single bucket. Once an object spans more than one bucket, every subsequent allocation is classified into one of two types:

1.  **Dkey Allocation:** Space for data rooted under a specific dkey (e.g., akey trees, akey records, value trees). These are allocated from a bucket dedicated to that dkey.

2.  **Object Shared Allocation:** Space for data shared across all dkeys of an object (e.g., dkey tree nodes, dkey records, etc.).

For "*Object Shared Allocation*", the logic detailed in the previous section applies. For "*Dkey Allocation*", a dkey's initial allocation uses *umem_allot_mb_evictable()* to provision its first E bucket, and the resulting bucket ID is stored in *vos_krec_df*. Subsequent allocations for the same dkey draw space from this assigned bucket using *umem_alloc_from_buckets()*. If the bucket lacks sufficient space to satisfy a request, the allocation automatically spills over to the NE zone.

An extra *allocation_type* parameter will be added to *umem_allot_mb_evictable()* and *umem_alloc_from_buckets()*, enabling the allocator to segregate buckets based on allocation type.

### Object Pin

The current VOS object cache manages concurrent object allocation and pinning while preventing deadlocks. When a caller requires access to an object, it invokes *vos_obj_acquire()* to hold the object cache, which pins the entire set of object buckets.

With the introduction of the partial object loading feature, *vos_obj_acquire()* will accept an additional dkey parameter and perform a dkey probe to determine the target dkey bucket ID. This optimization allows the function to pin only the shared object buckets and the specific dkey bucket.

Like the object cache, a new dkey cache will be introduced to manage concurrent dkey allocation. This dkey cache is establishing during *vos_obj_acquire()* and is rooted within the object cache. To maintain efficiency, the capacity of the dkey cache will scale proportionally with the expected volume of concurrent dkey accesses.

The *vos_pin_objects()* function, used for multi-object operations, will also be adapted to accept the targeted dkeys as a parameter and perform the corresponding probe operations.

## Spillover Evictable Bucket

SOE buckets are a special class of E-buckets used as spillover targets when an object's existing buckets are exhausted. They are permanently pinned in memory and are shared across all objects as a ready-to-use allocation reservoir. When a SOE bucket nears capacity, it is converted to a standard E bucket, and a replacement SOE bucket is drawn from either the free buckets or nearly empty E buckets. The number of SOE buckets scales dynamically with the number of available memory buckets.

**Design principles:**

-   **Permanently Pinned:** Keeping SOE buckets in memory avoids the architectural complexity and latency spikes associated with loading buckets during a transaction.

-   **Bounded Capacity:** Restricting the total number of SOE buckets ensures a predictable memory footprint.

-   **Allocator-Managed:** The allocator maintains SOE buckets transparently to VOS. (Further design details are covered in the allocator design document.)

## Eviction Thrashing Prevention

Traversal operations that visit objects in OID order (the physical layout of the object B-tree) cause their underlying buckets to be accessed randomly. Because the memory cache holds only a limited number of buckets simultaneously, consecutive object lookups can repeatedly evict and reload the same buckets. This inefficiency triggers severe eviction thrashing.

Resolving this thrashing issue is critical for two traversal workloads: recursive object iteration and GC (Garbage Collection).

### Recursive Object Iterator

The recursive object iterator is an internal component used exclusively by EC aggregation, VOS aggregation, the scrubber, and defragmentation (a planned future feature). Based on its usage, we can define the core requirements for the recursive object iterator, which are critical for the anti-thrashing design:

-   **No OID Ordering Required**: The iteration doesn't need to follow Object ID (OID) order.

-   **High Tolerance for Minority Edge Cases:** Skipping newly created objects or encountering minor duplication is acceptable.

-   **Zero Tolerance for Missing Data:** Skipping older, established objects is strictly unacceptable.

-   **Thrashing Mitigation:** Eviction thrashing must be minimized.

To address these requirements within the single-E-bucket per object implementation, *vos_iterate_obj()* was introduced as the entry point for the anti-thrashing recursive object iterator. It executes N rounds of standard object iteration (where N represents the total number of E buckets). In each round, the iterator processes only the objects belonging to one specific bucket while filtering out all others.

The same strategy will be maintained in the multi-E-bucket scenario. For an object spanning multiple E buckets, the first bucket is designated as primary bucket, and *vos_iterate_obj()* will iterate through objects in the order of their primary bucket IDs. While this approach minimizes thrashing overall, a manageable level of eviction thrashing may still occur when handling objects across multiple E buckets.

In contrast, dkey iteration strictly requires adhering to dkey order (following the physical layout of the dkey B-tree). Consequently, we must tolerate eviction thrashing when iterating into a massive object. However, because the total number of NE buckets decreases when multi-E-bucket per object support is enabled, significantly more memory buckets become available. As a result, the thrashing issue will be noticeably less severe than it is in the current implementation.

### GC

In the current single-E-bucket per object implementation, the anti-thrashing GC entry point, *gc_reclaim_pool_p2()* flattens the container first. This places all objects into GC bins indexed by their bucket ID. The GC then reclaims objects in sequential bucket ID order to eliminate eviction thrashing.

The same strategy is applied to multi-E-bucket per object scenario. Here, *gc_reclaim_pool_p2()* flattens the container to place all objects in GC bins indexed by each object's primary bucket ID. During object reclamation, the system follows one of two paths based on object size:

-   **Small Object (\<= N E-buckets):** The legacy object reclamation logic is maintained. While this keeps space overhead low, a manageable level of eviction thrashing may still occur.

-   **Large Objects (> N E-buckets):** The reclamation logic flattens the entire object first, placing all its dkeys into GC bins indexed by their respective dkey bucket IDs. This approach requires more space during large object reclamation but minimizes eviction thrashing.

# Implementation Phases

## Phase 1: Refactor VOS to Support Multiple E Buckets per Object

-   Modify the persistent on-disk data structures to accommodate multi-E-bucket metadata.

-   Implement and update the required allocator interfaces for multi-bucket lifecycle management.

-   Refactor object allocation, tracking, and object-pinning execution paths to seamlessly handle multi-bucket lookups and locks.

## Phase 2: Partial Object Loading

-   Modify on-disk data structures to track dkey bucket metadata persistently.

-   Implement and update required allocator interfaces.

-   Introduce a dedicated dkey cache and implement dkey probing logic prior to initiating transactions.

## Phase 3: Eviction Thrashing Prevention

-   Optimize recursive object iterator to minimize cache thrashing.

-   Optimize GC to minimize cache thrashing.

-   Profile and benchmark cache hit/miss rates before and after refactoring.

# Compatibility & On-disk Impact

## Persistent Layout

VOS object, dkey and GC durable formats are adjusted.

## Backward Compatibility

New features are restricted to newly created storage pools. Existing/legacy pools will operate in single-E-bucket mode without modification, ensuring uninterrupted operation and zero migration risk.

## Interoperability

-   **Pool Downgrades**: Not supported.

-   **Pool Upgrades**: Deferred. In-place conversion of legacy pools to the multi-E-bucket layout is out of scope for this release and will be addressed in a future milestone.

# External Interfaces

N/A

# Testing & Validation

-   Unit Tests:

    -   Implement test suites for new allocator API.

    -   Implement VOS unit tests for large object I/O operations.

    -   Implement VOS unit tests for large object GC.

-   Performance Benchmarks & Criteria:

    -   Small Objects (I/O & GC): Neutral impact.

    -   Large Objects (I/O): Prevent premature ENOSPC, throughput/latency regression \< 10%.

    -   Large Objects (GC): Reclamation speed regression \< 30%.

# Risks, Mitigations and Future Works

## Risks and Mitigations

## Risks and Mitigations

### 1. Object Fragmentation Overhead
* **Risk:** Through repeated spillovers, sporadic updates, key punches, and VOS aggregation, objects can accumulate far more E-buckets than their live data requires. Over time, this degradation creates **"sparse objects"**—data fragmented across an unnaturally high number of buckets. Pinning these fragmented objects prior to transactions introduces excessive, non-productive CPU and memory overhead.
* **Mitigations:**
  * **Preventative (Allocator Optimization):** Tune the allocation strategy to reduce fragmentation rates during dynamic writes and spillovers.
  * **Reactive (Background Defragmentation Service):** Implement an object defragment service that actively scans for sparse objects and consolidates their live data into the minimal required number of buckets *(future release)*.

### 2. Memory Exhaustion during Pinning
* **Risk:** When available memory buckets are constrained, attempting to load or pin a highly sparse object spanning numerous E-buckets will fail.
* **Mitigation:** Enforce a hard ceiling on the maximum number of E-buckets a single object can span. Set this upper bound strictly below the minimum number of reserved system memory buckets to guarantee that any valid object can always be loaded.

## Future Works

### Object Defragmentation

With the multiple E buckets per object design, an object can accumulate many E buckets through spillovers. Over time, sporadic updates, key punches and VOS aggregation can result in \'sparse objects\' - objects distributed across far more E buckets than their live data requires. This introduces unnecessary overhead when pinning objects prior to a transaction.

To mitigate this spare object issue, a background compaction service called 'object defrag' will be introduced. This service will identify sparse objects and consolidate their live data into the minimum number of buckets required.

### Flat Object Format

VOS objects are structured using a five-level B+/EV tree hierarchy. Consequently, updating a tiny object just once incurs massive metadata overhead due to multiple allocations across key/value tree nodes, records. For WORM (Write-Once-Read-Many) objects, this overhead permanently wastes meta blob space. Additionally, because these small allocations could map to different buckets, they cause object fragmentation, leading to unnecessary loading overhead when accessing tiny objects.

To optimize WORM workloads, newly created objects bypass the full B+ tree hierarchy and are initially packed into a single, contiguous flat record of key-value tuples. If a subsequent update occurs, the object is automatically promoted to the standard tree format before the update is applied. This approach introduces minor write amplification on the second update but significantly reduces overhead for single-write objects.
