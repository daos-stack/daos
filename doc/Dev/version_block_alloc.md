# Versioned Block Allocator

The Versioned Block Allocator is used by VOS for managing blocks on NVMe SSD, it's basically an extent based block allocator specially designed for DAOS.

## Allocation metadata

The blocks allocated by VEA are used to store single value or array value in VOS. Since the address and size from each allocation is tracked in VOS index trees, the VEA allocation metadata tracks only free extents in a btree, more importantly, this allocation metadata is stored on SCM along with the VOS index trees, so that block allocation and index updating could be easily made into single PMDK transaction, at the same time, the performance would be much better than storing the metadata on NVMe SSD.

## Scalable concurrency

Thanks to the shared-nothing architecture of DAOS server, scalable concurrency isn't a design cosideration for VEA, which means VEA doesn't have to split space into zones for mitigating the contention problem.

## Delayed atomicity action

VOS update is executed in a 'delayed atomicity' manner, which consists of three steps:
<ol>
<li>Reserve space for update and track the reservation transiently in DRAM;</li>
<li>Start RMDA transfer to land data from client to reserved space;</li>
<li>Turn the reservation into a persistent allocation and update the allocated address in VOS index within single PMDK transaction;</li>
Obviously, the advantage of this manner is that the atomicity of allocation and index updating can be guaranteed without an undo log to revert the actions in first step.

To support this delayed atomicity action, VEA maintains two sets of allocation metadata, one in DRAM for transient reservation, the other on SCM for persistent allocation.
</ol>

## Allocation hint

VEA assumes a predictable workload pattern: All the block allocate and free calls are from different 'IO streams', and the blocks allocated within the same IO stream are likely to be freed at the same time, so a straightforward conclusion is that external fragmentations could be reduced by making the per IO stream allocations contiguous.

The IO stream model perfectly matches DAOS storage architecture, there are two IO streams per VOS container, one is the regular updates from client or rebuild, the other one is the updates from background VOS aggregation. VEA provides a set of hint API for caller to keep a sequential locality for each IO stream, that requires each caller IO stream to track its own last allocated address and pass it to the VEA as a hint on next allocation.
