# Object

DAOS object stores user's data, it is identified by object ID which is unique
within the DAOS container it belongs to. Objects can be distributed across any
target of the pool for both performance and resilience. The diagram below
helps to understand where DAOS objects sit in the storage hierarchy.

![/docs/graph/Fig_002.png](/docs/graph/Fig_002.png "object in storage model")

The object module implements the object I/O stack. To avoid scaling problems
and overhead common to traditional storage stack, DAOS objects are
intentionally very simple. No default object metadata beyond the object type
and class are provided. This means that the system does not maintain expensive
attributes like access time, size, owner or permission and does not track object
openers.

## KV store, dkey and akey

Each DAOS object is a Key-Value store with locality feature. The key is split
into a <b>dkey</b> (distribution key) and an <b>akey</b> (attribute key). All
entries with the same dkey are guaranteed to be collocated on the same target.
Enumeration of the akeys of a dkey is provided.

The value can be either atomic <b>single value</b> (i.e. value replaced on
update) or an <b>array value</b> (i.e. arbitrary extent fetch/update).

## Object Type

The object type defines the key type and, in some cases, the value type too.
This allows the engine to optimize the underlying storage and offer specific
ordering guarantee for key enumeration. The daos\_otype\_t C enum defines all
the current supported object types. The default one is DAOS\_OT\_MULTI\_HASHED
that does not impose any type of the akey or dkey and can store either a
single value or array value under an akey. Lexical and integer (i.e. UINT64)
keys are supported. The KV object type provides a simplified data model
bypassing the akey and allowing only single single, while array object stores
array chunks under integer dkeys.

## Object Class

The DAOS <b>object class</b> describes the object distribution and protection
methods. An <b>object class</b> is represented by a unique 8-bit class ID
defining the data protection scheme (e.g. 2-way replication or erasure code 8+2,
see enum daos\_obj\_redun) combined with a 16-bit integer encoding the number
of groups (also called shards) over which the dkeys are distributed.

The DAOS API provides some pre-defined identifiers for the most common object
class. For instance, OC\_S1 can be used for object without data protection
distributed across a single target. OC\_S2 for two targets and OC\_SX for a
distribution across all the available targets in the pool. OC\_RP\_2G1 for
2-way replication over one target and OC\_RP\_5GX for 5-way replication over all
the available targets. Similarly, OC\_EC\_2P1G1 can be used for 2+1 Reed-Solomon
erasure code over one target and OC\_EC\_16P2GX for 16+2 over all the available
targets. See the full list in [daos_obj_class.h](/src/include/daos_obj_class.h)
for more information.

The object class ID and number of groups are embedded in object ID.
By `daos_obj_generate_oid()` user can generate an object ID for the specific object
class. DAOS uses this encoded information to generate the object layout that
describes on what targets the object is effectively stored.

Users can select the object class manually when generating the oid.
However manually selecting the object class is not encouraged for regular users and
should be done by advanced users only who understand all the trade-offs.
For most users, passing an OC_UNKNOWN (0) object class to `daos_obj_generate_oid()`
would allow DAOS to automatically select an object class based on the container
properties where that object is being accessed such as the redundancy factor (RF),
the number of domain (server engines) of the pool, and on the type of object
being accessed (determined by the feats flag).

The following details how the object class is chosen when no default or hints are provided:
- RF:0
  - Array, Byte Array, Flat KV object: OC_SX
  - no feats type: OC_S1
- RF:1
  - Array, Byte Array:
    - domain_nr >= 10 : OC_EC_8P1GX
    - domain_nr >= 6 : OC_EC_4P1GX
    - OC_EC_2P1GX
  - Flat KV object: OC_RP_2GX
  - no feats type: OC_RP_2G1
- RF:2
  - Array, Byte Array:
    - domain_nr >= 10 : OC_EC_8P2GX
    - domain_nr >= 6 : OC_EC_4P2GX
    - OC_EC_2P2GX
  - Flat KV object: OC_RP_3GX
  - no feats type: OC_RP_3G1
- RF:3
  - Array, Byte Array, Flat KV object: OC_RP_4GX
  - no feats type: OC_RP_4G1
- RF:4
  - Array, Byte Array, Flat KV object: OC_RP_6GX
  - no feats type: OC_RP_6G1

In addition, the oid generation API provides an optional mechanism for users to provide hints to the
DAOS library to control what redundancy method is chosen and what scale of groups to use for the
oclass without needing to specify the oclass itself. Those hints will override the auto class
selection for that particular setting. For example, one could set a redundancy hint for replication
on an Array object, and DAOS in this case will select the proper replicated object class instead of
the default EC one.

The user can specify any of the following redundancy hints:
- DAOS_OCH_RDD_DEF - Use RF prop (default)
- DAOS_OCH_RDD_NO  - No redundancy
- DAOS_OCH_RDD_RP  - Replication
- DAOS_OCH_RDD_EC  - Erasure Code

and any of the following sharding hints (percentage based on number of targets):
- DAOS_OCH_SHD_DEF  - use 1 grp (default)
- DAOS_OCH_SHD_TINY - <= 4 grps
- DAOS_OCH_SHD_REG  - max(128, 25%)
- DAOS_OCH_SHD_HI   - max(256, 50%)
- DAOS_OCH_SHD_EXT  - max(1024, 80%)
- DAOS_OCH_SHD_MAX  - 100%

### Object class naming conventions

DAOS object supports two data protection methods: replication(RP) and Erasure Code(EC).
A set replicated shard, or a set of data and parity shards belonging to the same parity group is
called redundancy group. An object can be chunked into many redundancy groups in order to achieve
higher I/O concurrency for better performance and large capactiy because redundacy groups spread
across different storage engines.

DAOS has over a hundred pre-defined object classes and specific naming conventions for these
classes:
- OC: Object Class
- RP: Replication, the number after underscore is number of replicas. For example: OC_RP_2GX
  represents two replicas
- EC: Erasure Code, the number before P is number of data shards, the number after P is number of
  parity shards in a parity group. For example, OC_EC_4P2G1 represents EC(4+2).
- G: Redundancy Group, a redundancy group can either be a set of replicated shards or a parity
  group of EC. The number after G is number of redundancy groups, "X" means the object should
  spread across as many engines as possible.
- If there is no RP or EC in the class name, the class has no data protection, in this case the
  postfix is S{n}, the number after S is number of object shards. similarly, a sharded object can
  horizontally scale I/O performance and capacity of the object.

### Maximum layout and limitations

DAOS has a few object classes with SX or GX as postfix, for example: OC_SX, OC_RP_2GX. As described
in the previous section, "X" represents maximum and SX or GX means the object should be placed in
possibly maximum number of engines in the storage pool.

The caveat is that DAOS will encode the actual number of shards or redundancy groups in the object
ID generated by API daos_obj_generate_oid(). It means that even if the pool size horizontally
growed by adding more storage engines, the already existent objects cannot be redistributed to
more engines than the original.

## Data Protection Method

Two types data protection methods supported by DAOS - replication
and erasure coding.
In addition, checksums can be used with both methods to ensure
end-to-end data integrity. If checksums discovers silent data corruption,
the data protection method (replication or erasure codes) might be able
to recover the data.

### Replication

Replication ensures high availability of object data because objects are
accessible while any replica exists. Replication can also increase read
bandwidth by allowing concurrent reads from different replicas.

DAOS relies on servers-side replication, which has stronger consistency of
replicas with a trade-off in performance and latency. DAOS client selects a
leader shard to send the IO request with the need-to-forward shards embedded
in the RPC request, when the leader shard gets that IO request it handles it
as below steps:
-   firstly forwards the IO request to others shards
    For the request forwarding, it is offload to the vos target's offload
    xstream to release the main IO service xstream from IO request sending and
    reply receiving (see shard_req_forward).
-   then serves the IO request locally
-   waits the forwarded IO's completion and reply client IO request.

The DAOS client-side IO error handling is relative simpler because all
operations only sent to only one server shard target, so need not to compare
replied pool map version from multiple shard targets, other error handing is
same as client replication mode described above.

Conflictng writes can be detected and serialized by the leader shard server.

### Erasure Code

In the case of replicating a whole object, the storage overhead would be 100%
for each replica. This is unaffordable in some cases, so DAOS also provides
erasure code as another option of data protection, with better storage
efficiency.

Erasure codes may be used to improve resilience, with lower space overhead. This
feature is still working in progress.

### Checksum

The checksum feature attempts to provide end-to-end data integrity. On an update,
the DAOS client calculates checksums for user data and sends with the RPC to
the DAOS server. The DAOS server returns the checksum with the data on a fetch
so the DAOS client can verify the integrity of the data. See [End-to-end Data
Integrity Overiew](../../docs/overview/data_integrity.md) for more information.

Checksums are configured at the container level and when a client opens a
container, the checksum properties will be queried automatically, and, if
enabled, both the server and client will init and hold a reference to a
[daos_csummer](src/common/README.md) in ds_cont_hdl and dc_cont respectively.

For Array Value Types, the DAOS server might need to calculate new checksums for
requested extents. After extents are fetched by the server object layer, the
checksums srv_csum

#### Object Update

On an object update (`dc_obj_update`) the client will calculate checksums
using the data in the sgl as described by an iod (`daos_csummer_calc_iod`).
Memory will be allocated for the checksums and the iod checksum
structures that represent the checksums (`dcs_iod_csums`). The checksums will
be sent to the server as part of the IOD and the server will store in [VOS]
(src/vos/README.md).

#### Object Fetch - Server

On handling an object fetch (`ds_obj_rw_handler`), the server will allocate
memory for the checksums and iod checksum structures. Then during the
`vos_fetch_begin` stage, the checksums will be fetched from
[VOS](src/vos/README.md). For Array Value Types, the extents fetched will
need to be compared to the requested extent and new checksums might need
to be calculated. `ds_csum_add2iod` will look at the fetched bio_sglist and
the iod request to determine if the stored checksums for the request
are sufficient or if new ones need to be calculated.


##### `cc_need_new_csum` Logic

The following are some examples of when checksums are copied and when
new checksums are needed. There are more examples in the unit tests for this
logic( ./src/object/tests/srv_checksum_tests.c)
```
     Request  |----|----|----|----|
     Extent 2           |----|----|
     Extent 1 |----|----|
```
> Chunk length is 4. Extent 1 is bytes 0-7, extent 2 is bytes 8-15. Request is
 bytes 0-15. There is no overlap of extents and each extent is completely
 requested. Therefore, the checksum for each chunk of each extent is copied.
---
```
     Request  |----|----|----
     Extent 2 |    |----|----
     Extent 1 |----|----|
```
> Chunk length is 4. Extent 1 is bytes 0-7. Extent 2 is bytes 8-11. Request is
 bytes 0-1. Even though there is overlap, the extents are aligned to chunks,
 therefore each chunk's checksum is copied. The checksum for the first chunk
 will come from extent 1, the second and third checksums come from extent 2,
 just like the data does.
---
```
     Request  |  ----  |
     Extent 1 |--------|
```
> Chunk length is 8. Extent 1 is bytes 0-7. Request is bytes 2-5. Because the
 request is only part of the stored extent, a new checksum will need to be created

---
```
     Request  |--------|--------|
     Extent 2 |   -----|--------|
     Extent 1 |------  |        |
```
> Chunk length is 8. Extent 1 is bytes 0-5. Extent 2 is bytes 3-15. Request
 is bytes 0-15. The first chunk needs a new checksum because it will be
 made up of data from extent 1 and extent 2. The checksum for the second
 chunk is copied.

Note: Anytime the server calculates a new checksum; it will use the stored
checksum to verify the original chunks.

#### Object Fetch - Client

In the client RPC callback, the client will calculate checksums for the
data fetched and compare to the checksums fetched (`daos_csummer_verify`).

## Object Sharding

DAOS supports different data distribution strategies.

### Single (unstriped) Object

For replication, single (unstriped) object always has one stripe and each
shard of it is a full replica, for Erasure code, single object only has one
parity group and a shard of it can either be a data chunk or parity chunk
of the parity group.
A single (unstriped) object can be either a byte-array or a KV.

### Fixed Stripe Object

A fixed stripe object has a constant number of stripes and each stripe has a
fixed stripe size. These stripe attributes are predefined by object class, DAOS
uses these attributes to compute object layout.

### Dynamically Striped Object (Not implemented)

A fixed stripe object always has the same number of stripes since it was
created. In contrast, a dynamically stripped object could be created with a
single stripe. It will increase its stripe count as its size grows to some
boundary, to achieve more storage space and better concurrent I/O performance.

Now the dynamically Striped Object is not implemented yet.
