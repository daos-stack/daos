# Self-healing

**Topiccs in this Chapter:**

- <a href="#10.5">Rebuild</a>
- <a href="#10.6">Rebalance</a>

<a id="10.5"></a>
## Rebuild

When a target failed, the RAS system should promptly detect it and notify the Raft leader about the failure, the Raft leader can then collectively broadcast the failure event to all surviving targets in the DAOS pool.

Because DAOS-SR is using algorithm to compute object distribution, it means that a target can compute out layout of an object with a few metadata:

- Object ID (Object class ID is embedded in object ID)
- The affinity target ID
- Hash stride

These metadata are tiny enough to be replicated for all object shards. Based on these metadata, any of surviving targets can independently calculate out redundancy group (RDG) members for its local object shards. If the RDG of a local object shard has a missing shard on the failed target, then data of that missing shard, in all existing epochs, should be rebuilt from the surviving shards in the RDG. To reconstruct missing object shards, DAOS-SR needs different methods to rebuild object protected by replication and erasure code, these methods are discussed in the section Rebuild Methods.

Moreover, in order to coordinate the cluster-wide rebuild process, DAOS-SR should choose one of these two models as the generic rebuild protocol:

- Pull model: A surviving target (rebuild contributor) sends the object list to the rebuild targets (rebuild initiator). A rebuild initiator can iterate the received object list and actively pull data from the contributors and reconstruct missing object shards. This approach needs extra communication to generate the object list on the initiators, but it is simpler for multi-failure rebuild.
- Push model: After determining the objects to be rebuilt, a surviving target directly sends data of these objects to rebuild targets to recover missing object shards. This approach has less communication overhead because it eliminates the step of building the object list on the remote destination, so it is more efficient for rebuilding after a single failure, but it could be difficult to support multi-failure rebuild.

This design will choose the ‚pull model‚ to provide a better solution for multi-failure; please see the sections Rebuild Protocol and Multi-Failure for details.

<a id="10.5.1"></a>
### Failure Detection

Failure detection of DAOS relies on the RAS system, which should actively notify DAOS servers of the failure. As described in the sections MAKEREF"Fault Detection and Diagnosis" and MAKEREF"Fault Isolation", all surviving servers can learn the RAS failure event as a pool map change from the Raft leader; they should start the rebuild protocol straightaway. There is no direct failure notification to clients, however, all DAOS clients can eventually learn about the pool map change from their I/O peering servers. They should abort all inflight I/O requests against the faulty target, and switch to the new pool map for all future I/Os:

- They should not read from the failed target anymore (degraded read).
- They should write to the rebuild target that is replacing the failed one.

Although it is possible for a node to detect failure without RAS, however, in order to simplify the failure handling protocol in the current design, a node should infinitely retry the communication with the problematic peer until it gets the pool map change.

In addition, because the failure notification from the Raft leader can arrive at different servers in different order, so some servers and even clients may learn of the event earlier than other servers. In this case, a server may also see the pool map change notification from an I/O message, either from any client or other servers in rebuild, because all I/O messages should carry the pool map version cached on the sender.

<a id="10.5.2"></a>
### Global rebuild status

When the Raft leader receives a failure notification from the RAS system, it should update the pool map for the failure, and collectively broadcast a rebuild request, which carries the pool map change to all DAOS targets. Any target that receives the rebuild request should either start the rebuild process if it is not running yet, or check the rebuild status of the corresponding failure and return the status to the Raft leader. The rebuild status includes gross information such as the number of objects to be rebuilt, the number of rebuilt objects, time already consumed on the rebuild, etc.

The Raft leader can then extract the rebuild status for each target from the collective replies and put them in a status table stored in volatile memory, so the administrator is able to query the overall status at any time. It is unnecessary to store the global rebuild status persistently, because if the Raft leader failed, a new elected leader can collect the global rebuild status using a collective broadcast.

<a id="10.5.3"></a>
### Object Shard Index Table and Rebuild Log

Although there is no persistently stored global rebuild status, each rebuild target has a persistent rebuild log. In the PULL model, the rebuild log of a target stores IDs and metadata of all objects that need to be rebuilt on this target. While the rebuild is underway, a target also can store the progress in the rebuild log at the granularity of object. In this way, if the rebuild was suspended or interrupted the rebuild can be continued based on the rebuild log.

To generate the rebuild log, each target should maintain an Object Shard Index Table (OSIT) for each container shard on this target.

<a id="10.5.3.1"></a>
#### Object Shard Index Table (OSIT)

At the VOS level, each container shard has an Object Shard Index Table (OSIT) to index all object shards within this target. In addition to the interfaces to update/search object shard by ID, this table also provides interfaces to load and store object shard metadata from upper layers.

DSR can use this table to save its object metadata, when failure happened, DSR should be able to find out objects that need to be rebuilt by scanning IDs and their metadata in OSITs instead of querying OITs, which could be remotely stored. By this way, DSR could significantly reduce communications and latency of rebuild.

In order to reduce storage overhead, the object metadata stored in the table should be minimized and just sufficient for computing layout of objects by the placement algorithm. It means those metadata that can be generated by algorithm or changed overtime. For example, a split version of a KV object or even the explicitly enumerated layout, should not be stored in this table.

Here are the object shard metadata stored in OSIT record:

- Object ID (object class ID is embedded in it)
- Object shard index
- Object hash stride
- ID of the affinity target for dynamically stripped object (or unstriped object)

<a id="10.5.3.1"></a>
#### Rebuild Log

To generate the rebuild log, the DAOS-SR server needs to scan each record in an OSIT, and calculate the layout and determine the redundancy group (RDG) of the object shard. If the faulty target is a member of the RDG of the object shard, then DSR should compute out the rebuild target for the faulty target, and send the OSIT record for this object record to the rebuild target, which will store it into its rebuild log and process it later.

In the example in the <a href="#f10.17">figure</a> below, with the metadata record store in the OSIT, target-33 can compute out the RDG of shard-2 of object-23, which is target-[40, 3, 33]. Because this RDG does not include the failed target-251, so target-33 will skip this record. However, the RDG of shard-7 of object-79 is target-[251, 33, 19], which includes the failed target-251, so target-33 should send this record to the spare target-18, which will add the record to its rebuild log.

<a id="f10.17"></a>
**Generate rebuild-log for a faulty target**

![../../doc/graph/Fig_058.png](../../doc/graph/Fig_058.png "Generate rebuild-log for a faulty target")

After this process is complete, a target may have received a list of objects that need to be rebuilt on this target from different contributors. It should store them in the rebuild log, which is a KV object indexed by object ID and object shard ID. Each failure event should have an individual rebuild log, The ID of the rebuild log (KV object) can be made up from the failure sequence number.

As mentioned previously, to support progressive rebuilding and allow resuming from an interrupted recovery, records in the target rebuild log can also be used to store fine-grained rebuild processes. For example, epoch and extents that have been recovered for each particular object shard. While resuming the interrupted rebuild, because DAOS I/O operations are idempotent, so it is safe to re-execute some log records even if the log status cannot reflect the real progress accurately.

The target rebuild log should be deleted when the Raft leader broadcasts the completion of rebuild for a particular failure.

<a id="10.5.4"></a>
### Rebuild Methods

While reconstructing a missing object shard, the rebuild target (rebuild initiator), which owns the rebuild log for this object shard, can algorithmically determine the source targets (rebuild contributor) for rebuilding the object shard, and call the standard DAOS-M object I/O APIs to concurrently pull different portions of the object shard from those contributors. This section introduces the methods for determining data sources and reconstructing data for both erasure code and replication.

This section will not cover the solution for online rebuild and data consistency issues; it will be discussed in a dedicated section for Online Rebuild.

<a id="10.5.4.1"></a>
#### Rebuild replicated object

For objects protected by replication, although a missing replica can be rebuilt fully from any of the surviving replicas, however, pulling all data from a single replica could overload the target that holds the replica and inject large performance jitter to the storage cluster. Therefore, the rebuild initiator should concurrently read from all surviving replicas to distribute rebuild workloads.

In the example in the <a href="#f10.17">figure</a> above, target-18 is going to rebuild the missing object for the failed target-251 of the RDG[251, 33, 19], as there are still two surviving replicas on target-33 and target-19, so target-18 can pull extents [0-16M], [32M-48M], [64M-80M]‚¶ of the object from target-33, and pull extents [16M-32M], [48M-64M], [80M-96M]‚¶ of the object from target-19.

Because the striping strategy of a DSR KV object is based on consistent hashing, so the way of distributing rebuild workloads could use the same approach: the rebuild initiator can read different hashed key ranges from different replicas (contributors).

<a id="10.5.4.2"></a>
#### Rebuild erasure coded object

In order to rebuild object protected by erasure code, the rebuild initiator should always pull data from multiple sources, because the missing object shard has to be reconstructed from multiple object shards in the RDG.

As described in the section <a href="#10.4.2">"Erasure Code"</a>, even in the same RDG, group member indexes of different stripes are different. It means that after the initiator pulled chunks from a RDG, it needs to calculate out the group member indexes of these chunks based on their offsets, and then compute the missing chunk, which could be either data or parity, from the pulled chunks and their indexes.

Also worth mentioning is a rebuild initiator has all the compute workloads of erasure coded objects. However, with the technology described in the section <a href="#10.2.2.2.3">‚Load Balancing for Failure‚</a>, objects of the faulty target can be rebuilt on many different rebuild targets. In other words, many rebuild initiators are simultaneously computing, so the overall compute workloads are still widely distributed in the storage cluster.

<a id="10.5.5"></a>
### Rebuild Protocol

Previous sections have already introduced major components and mechanisms for rebuild. This section summarizes and describes the generic protocol of DSR rebuild. It focuses on the protocol for handling a single failure, because there will be another section to introduce Multi-Failure.

As described earlier, when the Raft leader receives a RAS notification about a target failure, it could propagate the failure notification to all surviving targets by a collective RPC. Any target that receives this notification should start to scan its OSITs (Object Shard Index Table (OSIT)) to determine the objects that lost data redundancy on the faulty target, and then send their IDs and metadata to the Rebuild Targets (rebuild initiator). In this step, the OIT (Object Index Table) object should be checked as a regular object, because it is essentially a 3-way replicated KV object, which may also lose data redundancy.

<a id="f10.18"></a>
**Rebuild Protocol**
![../../doc/graph/Fig_059.png](../../doc/graph/Fig_059.png "Rebuild Protocol")

The <a href="#f10.18">figure</a> above is an example of this process: There are five objects in the cluster: object A is 3-way replicated, object B, C, D and E are 2-way replicated. When target-2 failed, target-0, which is the Raft leader, broadcasted the failure to all surviving targets to notify them to enter the degraded mode and scan OSITs:

- Target-0 found that object D lost a replica and calculated out target-1 is the rebuild target for D, so it sent object D‚s ID and its metadata to target-1.
- Target-1 found that object A lost a replica and calculated out target-3 is the rebuild target for A, so it sent object A‚s ID and its metadata to target-3.
- Target-4 found objects A and C lost replicas and it calculated out target-3 is the rebuild target for both objects A and C, so it sent IDs for objects A and C and their metadata to target-3.

After receiving these object IDs and their metadata, target-1 and target-3 can compute out surviving replicas of these objects, and rebuild these objects by pulling data from these replicas.

During a rebuild, a target should not actively send rebuild progress report to the Raft leader, instead it should report its status only if it got the periodical collective RPC for the same failure from the Raft leader.

After all targets have finished rebuild from the failure, the Raft leader should propagate the final collective RPC to all targets and notify them to delete their local rebuild logs for this failure, and exit from the degraded mode

<a id="10.5.6"></a>
### Online Rebuild

When the Raft leader broadcasts the rebuild request and pool map change to other targets, some of those targets might learn about this information earlier than other targets. In addition, a client can only learn about the pool map change when it has I/O against a server that has already known the new pool map. Therefore, there are always gaps between the time points that different nodes get the new pool map.

These gaps are okay if there is no concurrent I/O while rebuilding, because all targets will be involved in the rebuilding process at some point, so all data can be rebuilt eventually.

However, if there are concurrent writes during rebuild, the rebuild protocol should guarantee that new writes will never be lost because of those gaps. Those writes should be either directly stored in the new object shard or pulled to the new object shard by the rebuild initiator. The protocol should also ensure the rebuild initiator does not pull those new writes again if it already has received them from clients.

<a id="10.5.6.1"></a>
#### Consistency

In order to achieve the goal described above, the DSR adopts a simple protocol:

- A DSR I/O request can complete only if underlying I/O requests against all RDG members succeeded.

More particular, this protocol is for the client side data protection:

- A DSR object update can complete only if updates of all the object shards of the RDG have successfully completed.
- If any of these updates failed, the DSR client should infinitely retry until it succeeds, or there is a pool map change which shows the target failed. In the second case, the DSR client should switch to the new pool map, and send the update to the new destination, which is the rebuild target of this object.

The <a href="#f10.19">figure</a> below shows an example of this client protocol.

Also worth mentioning is that this protocol is based on assumption that Raft leader can get the RAS notification and broadcast the failure to all targets in a reasonable short time. Otherwise, the DSR I/O request can hang for long time.

<a id="f10.19"></a>
**DSR client protocol for online rebuild**
![../../doc/graph/Fig_060.png](../../doc/graph/Fig_060.png "DSR client protocol for online rebuild")

For the server side data protection, there are many researches that introduce various protocols to provide data consistency, however, given the epoch and I/O model of DAOS, most of them are unnecessary:

- Server side erasure coding can only happen to already committed epoch, so there is no concurrent write for them.
- There is no client cache and distributed lock in DAOS, so server side replication can also be greatly simplified, for example, the primary replica can even use the similar protocol as the client replication to guarantee data consistency for online rebuild.

<a id="10.5.6.2"></a>
#### Data Fence

If an application keeps writing to the object that is in rebuilding, and the writing throughput is equal to or higher than the rebuild throughput, then the rebuilding process needs a fence for data, so it can complete the rebuild when all data have reached at the destination shard, otherwise the rebuilding process could be endless.

Because all I/O requests should carry the pool map version, the data fence can be the pool map version when an extent or KV was updated. The data fence should be stored as metadata of an extent or KV in VOS. The rebuilding process can skip those data fenced by the new pool map version.

The data fence could also be used to resolve the conflict between application writes and rebuilding writes: application writes should overwrite rebuilding writes in the VOS data tree even they are in the same epoch.

<a id="10.5.7"></a>
### Multi-Failure

In a large-scale storage cluster, multiple failures might occur when a rebuild from a previous failure is still in progress. In this case, DAOS-SR should neither simultaneously handle these failures, nor interrupt and reset the earlier rebuilding progress for later failures. Otherwise, the time consumed for rebuilds for each failure might grow significantly and rebuilds may never end if new failures overlap with ongoing rebuilds.

Before describing the protocol for multi-failure rebuild, a few terms that have already been referenced in earlier sections, should be better defined:

- Rebuild initiator: This is a target which owns rebuild log is called ‚rebuild initiator‚. In the rebuild process, it should rebuild objects in the rebuild log by pulling data from ‚rebuild contributors‚
- Rebuild contributor: A target is ‚rebuild contributor‚ if its objects lost data redundancy in a failure. A rebuild contributor should find out these objects and provide data for rebuild initiators, so initiators can recover redundancy for these objects.
- A target can be both initiator and contributor.

Armed with these concepts, the protocol for multi-failure is summarized as follows:

- If the later failed target is the rebuild initiator of the first failure, then the object shards being rebuilt on the initiator should be ignored, because the rebuild log and progress have been lost together with the initiator.
- If the later failed target is the rebuild contributor of the first failure, because it cannot contribute data anymore, those initiators that are pulling data from it should switch to other contributors and continue the rebuild for the first failure.
- A target in rebuild does not need to re-scan its objects or reset rebuild progress for the current failure if another failure has occurred.

The following <a href="#f10.20">figure</a> is an example of this protocol.

<a id="f10.20"></a>
**Multi-failure protocol**
![../../doc/graph/Fig_061.png](../../doc/graph/Fig_061.png "Multi-failure protocol")


- In this example, object A is 2-way replicated, object B, C and D are 3-way replicated.
- After failure of target-1, target-2 is the initiator of rebuilding object B, it is pulling data from target-3 and target-4; target-3 is the initiator of rebuilding object C, it is pulling data from target-0 and target-2.
- Target-3 failed before completing rebuild for target-1, so rebuild of object C should be abandoned at this point, because target-3 is the initiator of it. The missing data redundancy of object C will be reconstructed while rebuilding target-3.
- Because target-3 is also contributor of rebuilding object B, based on the protocol, the initiator of object B, which is target-2, should switch to target-4 and continue rebuild of object B.
- Rebuild process of target-1 can complete after finishing rebuild of object B. By this time, object C still lost a replica. This is quite understandable, because even if two failures have no overlap, object C will still lose the replica on target-3.
- In the process of rebuilding target-3, target-4 is the new initiator of rebuilding object C.

<a id="10.5.8"></a>
### Unrecoverable Failure

When there are multiple failures, if the number of failed targets from different domains exceeds the fault tolerance level of any schema within the container, then there could be unrecoverable errors and applications could suffer from data loss. For example, if there are a few objects protected by 2-way replication, then losing two targets in the different domains may cause data loss. In this case, upper layer stack software could see errors while sending I/O to the object that could have missing data. In addition, an administrator tool, which is not covered by this design, could scan all object classes of a container, and report if it could have lost data because of failures.

<a id="10.6"></a>
## Rebalance

Rebalance is the process of redistributing objects while extending a DAOS pool. As described in the section <a href="#10.2">"Algorithmic object placement"</a>, different placement algorithms have different rebalancing protocols. This section is based on the <a href="#10.2.2">"Ring Placement Map"</a> algorithm.

<a id="10.6.1"></a>
### Rebalance Protocol

Similarly with the rebuild protocol, rebalance of DAOS-SR relies on the RAS notification to discover the change of pool map, and relies on the Raft leader to propagate the change to other servers and control the global status of rebalance.

After the rebalance request, each of the original targets needs to scan its OSITs and determine objects to be migrated, and send their IDs and metadata to their new targets. Those new targets should create rebalance log for the received IDs and metadata, just like rebuild log. After that, those new targets can traverse all records in the rebalance log, and pull data from their original targets. The Raft leader should periodically send collective RPC to collective progress of rebalance, and send the completion notification when all targets have completed the rebalance.

In addition, similar to rebuild, two terminologies are used to describe failure handling of the rebalance:

- **Rebalance initiator:** A newly added target which owns and maintains the rebalance log, it actively pulls objects from "rebalance contributors".
- **Rebalance contributor:** A target which has objects to be moved out. A rebalance contributor should find out these objects and provide data for ‚rebalance initiators‚.

During a rebalance:

- If a rebalance initiator failed, then the DSR should do nothing for it. Because the rebalance log and progress on the initiator have been lost, attempts to recreate data for the failed target could significantly prolong the time consumption of rebalance. The lost data should be recovered by the later rebuild process.
- If a rebalance contributor failed, the initiator, or new target, should use redundant data on other contributors to finish the rebalancing process.

<a id="10.6.2"></a>
### Online rebalance

If there is no concurrent I/O, as previously described, the rebalance protocol is quite like the rebuild. However, if there are concurrent I/Os, the protocol should have extra mechanism to guarantee data consistency.

- Read request should see all data in existing committed epochs.
- Read request should see all newly completed updates.

In order to achieve these goals, before the global completion of rebalance, the protocol requires the DSR client to run in rebalancing mode:

- Send read requests to old targets because only old targets can guarantee to provide all versions of data in rebalancing mode.
- Send update requests to both new and original targets, so later reads can see new writes as well. This approach also needs the For the server side data protection, there are many researches that introduce various protocols to provide data consistency, however, given the epoch and I/O model of DAOS, most of them are unnecessary:
- Server side erasure coding can only happen to already committed epoch, so there is no concurrent write for them.
- There is no client cache and distributed lock in DAOS, so server side replication can also be greatly simplified, for example, the primary replica can even use the similar protocol as the client replication to guarantee data consistency for online rebuild.
- Data Fence to prevent the initiator from pulling new writes again, and guarantee that online rebalance can complete in finite time.

Therefore, the overhead of rebalancing mode is high, because it will double the space usage, and double the bandwidth consumption of writes. However, given the multi-version data model of DAOS, this is the most realistic and straightforward solution. In addition, this approach does not require an extra protocol for handling target failure during rebalance, because missing data always can be reconstructed from object shards stored on old targets.

Theoretically, after the new target has pulled all data of an object shard, the old target can delete the original object shard immediately. However, because data pulling of different shards of the same object are totally independent, so it is possible that some shards of an object have already been pulled to new targets, whereas remaining shards are still stored on old targets. In this case, it is difficult for a DSR client to find out data location of the algorithmically placed object. Considering tens of thousands of clients could send I/O requests to wrong destination and get rejection, the neat solution is retaining old object shards until the global completion of rebalance. Moreover, allowing mixed object layout would add more complexities to target failure handling of the rebalance protocol.

As described earlier, when the Raft leader finds that all targets have finished the rebalancing process, it should send a collective RPC as the global completion notification of the rebalance. After receiving the notification, the old targets should exit from the rebalancing mode and reject I/O requests to those migrated objects; the clients who learned of the completion from any server should also exit from the rebalancing mode and only deliver I/O request to new targets. However, given the asynchrony of collective RPC, the DSR protocol should handle some corner cases for this final step.

Let‚s examine this scenario: A new target has accepted some writes for an object from Client A, which has just exited from the rebalancing mode. In the meantime, an old target has not heard about the global completion yet, and it has served a read request of the object replica from Client B, who is also in the rebalancing mode. In this case, Client B cannot see the completed write from Client A.

In order to resolve this race condition, the DSR needs to add these steps to the rebalance  protocol:

- If a target has exited from the rebalancing mode, it should piggyback this information on I/O replies to clients.
- If a client in the rebalancing mode submits an update request to both old and new targets, and learns from their replies that any of those targets have exited from the rebalancing mode, this client should send extra pings to those targets that are still in the rebalancing mode, and request them to exit from the rebalancing mode. After that, this client itself should exit from the rebalancing mode, and complete the update operation.

In this way, the rebalance protocol can guarantee that old targets can always reject I/O requests if they have stale data. The following <a href="#f10.21">figure</a> is an example of this protocol.

<a id="f10.21"></a>
**Write in rebalancing mode**
![../../doc/graph/Fig_062.png](../../doc/graph/Fig_062.png "Write in rebalancing mode")

<a id="10.6.3"></a>
### No Multi-Rebalance

To simplify the design, multi-rebalance is not supported in this design. The user has to serialize pool-map extending operations.
