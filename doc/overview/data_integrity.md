# Introduction
Arguably, one of the worst things a data storage system can do is to return
 incorrect data without the requester knowing. While each component in the
 system (network layer, storage devices) may offer protection against silent
 data corruption, DAOS provides end-to-end data integrity using checksums to
 better ensure that user data is not corrupted silently.

For DAOS, end-to-end means that the client will calculate and verify checksums,
providing protection for data through the entire I/O stack. During a write or
update, the DAOS Client library (libdaos.so) calculates a checksum and appends
it to the RPC message before transferred over the network.
Depending on the configuration, the DAOS Server may or may not calculate checksums
to verify the data on receipt. On a fetch, the DAOS Server will send a known
good checksum with the requested data to the DAOS Client, which will calculate
checksums on the data received and verify.

## Requirements
### Key Requirements
There are two key requirements that DAOS will support.
1. Detect silent data corruption - Corruption will be detected on the
 distribution and attribute keys and records within a DAOS object. At a minimum,
 when corruption is detected, an error will be reported.
1. Correct data corruption - When data corruption is detected, an attempt will
 be made to recover the data using data redundancy mechanisms.

### Supportive/Additional Requirements
Additionally, DAOS will support ...
1. End to End Data Integrity as a Quality of Service Attribute - Container
 properties are used to enable/disable the use of checksums for data integrity
 as well as define specific attributes of data integrity feature.  See
 https://daos-stack.github.io/user/container/#data-integrity for details on
 configuring a container with checksums enabled.
1. Minimize Performance Impact - When there is no data corruption, the End to
 End Data Integrity feature should have minimal performance impacted. If data
 corruption is detected, performance can be impacted to correct the data.
 Work is ongoing to minimize performance impact.
1. Inject Errors - The ability to corrupt data within a specific record, key,
 or checksum will be necessary for testing purposes. Fault injection is used to
 simulate corruption over the network and on disk. The DAOS_CSUM_CORRUPT_*
 flags used for data corruption are defined in src/include/daos/common.h.
1. Logging - When data corruption is detected, error logs are captured in
 the client and server logs.

Up coming features not supported yet
1. Event Logging - When silent data corruption is discovered, an event should
 be logged in such a way that it can be retrieved with other system health and
 diagnostic information.
2. (Currently in progress) Proactive background service task - A background task
   on the server which scans for and detects (audits checksums) silent data
   corruption and corrects.
   
# Data Corruption
There are two main types of corruption that DAOS may detect. Depending on how
the data integrity feature is configured, DAOS may not know where the corruption
occurred. 
1. Data corruption that occurs over the network on an object update or fetch
   between a DAOS client and the DAOS IO Server.
2. Data corruption that occurs on media due to HW failures, power loss, etc.

## Checksum Error Counts
The following counters will be incremented each time a distinct data corruption
error is discovered. 
- **Checksum Media Errors**: Whenever a new error is detected and it is known it
  is a media error. This counter can be viewed with the NVMe Health Statistics,
  but needs to be modified to not increment every time a checksum error happens.
- **Checksum Network Error**: Whenever a new error is detected and is known to
  be over network (on server verify update or on client fetch when server verify
  fetch is enabled). (Note: doesn't exist yet)
- **Checksum General Error**: Whenever it's not known if it's a media or network
   error. Not sure where this counter will be. (Note: doesn't exist yet) 

# Keys and Value Objects
Because DAOS is a key/value store, the data for both keys and values is
protected, however, the approach for both is slightly different. For the two
different value types, single and array, the approach is also slightly
different.

## Keys
On an update and fetch, the client calculates a checksum for the data used
as the distribution and attribute keys and will send it to the server within the
RPC. The server verifies the keys with the checksum.
While enumerating keys, the server will calculate checksums for the keys and
pack within the RPC message to the client. The client will verify the keys
received.

!!! note
    Checksums for keys are not stored on the server. A hash of the key is
    calculated and used to index the key in the server tree of the keys
    (see [VOS Key Array Stores](../../src/vos/README.md#key-array-stores)).
    It is also expected that keys are stored only in Storage Class Memory which
    has reliable data integrity protection.

## Values
On an update, the client will calculate a checksum for the data of the value and
will send it to the server within the RPC. If "server verify" is enabled, the
server will calculate a new checksum for the value and compare with the checksum
received from the client to verify the integrity of the value. If the checksums
don't match, then data corruption has occurred and an error is returned to the
client indicating that the client should try the update again. Whether "server
verify" is enabled or not, the server will store the checksum.
See [VOS](../../src/vos/README.md) for more info about checksum management and
storage in VOS.

On a fetch, the server will return the stored checksum to the client with the
values fetched so the client can verify the values received. If the checksums
don't match, then the client will fetch from another replica if available in
an attempt to get uncorrupted data.

There are some slight variations to this approach for the two different types
of values. The following diagram illustrates a basic example.
 (See [Storage Model](storage.md) for more details about the single value
 and array value types)

![](../graph/data_integrity/basic_checksum_flow.png)

### Single Value
A Single Value is an atomic value, meaning that writes to a single value will
update the entire value and reads retrieve the entire value. Other DAOS features
such as Erasure Codes might split a Single Value into multiple shards to be
distributed among multiple storage nodes. Either the whole Single Value (if
going to a single node) or each shard (if distributed) will have a checksum
calculated, sent to the server, and stored on the server.

Note that it is possible for a single value, or shard of a single value, to
be smaller than the checksum derived from it. It is advised that if an
application needs many small single values to use an Array Type instead.

### Array Values
Unlike Single Values, Array Values can be updated and fetched at any part of
an array. In addition, updates to an array are versioned, so a fetch can include
parts from multiple versions of the array. Each of these versioned parts of an
array are called extents. The following diagrams illustrate a couple examples
(also see [VOS Key Array Stores](../../src/vos/README.md#key-array-stores) for
more information):

<div>
A single extent update (blue line) from index 2-13. A fetched extent (orange
line) from index 2-6. The fetch is only part of the original extent written.

![](../graph/data_integrity/array_example_1.png)
</div>

<div>
Many extent updates and different epochs. A fetch from index 2-13 requires parts
from each extent.

![Array Example 2](../graph/data_integrity/array_example_2.png)

</div>

The nature of the array type requires that a more sophisticated approach to
creating checksums is used. DAOS uses a "chunking" approach where each extent
will be broken up into "chunks" with a predetermined "chunk size." Checksums
will be derived from these chunks. Chunks are aligned with an absolute offset
(starting at 0), not an I/O offset. The following diagram illustrates a chunk
size configured to be 4 (units is arbitrary in this example). Though not all
chunks have a full size of 4, an  absolute offset alignment is maintained.
The gray boxes around the extents represent the chunks.

<img src="../graph/data_integrity/array_with_chunks.png" width="700" />

(See [Object Layer](../../src/object/README.md) for more details about the
checksum process on object update and fetch)

# Checksum calculations
The actual checksum calculations are done by the
 [isa-l](https://github.com/intel/isa-l)
and [isa-l_crypto](https://github.com/intel/isa-l_crypto) libraries. However,
these libraries are abstracted away from much of DAOS and a common checksum
library is used with appropriate adapters to the actual isa-l implementations.
[common checksum library](../../src/common/README.md#checksum)

# Performance Impact
Calculating checksums can be CPU intensive and will impact performance. To
 mitigate performance impact, checksum types with hardware acceleration should
 be chosen. For example, CRC32C is supported by recent Intel CPUs, and many are
 accelerated via SIMD.

# Quality
Unit and functional testing is performed at many layers.

| Test executable   | What's tested | Key test files |
| --- | --- | --- |
| common_test | daos_csummer, utility functions to help with chunk alignment  | src/common/tests/checksum_tests.c |
| vos_test | vos_obj_update/fetch apis with checksum params to ensure updating and fetching checksums | src/vos/tests/vts_checksum.c |
| srv_checksum_tests | Server side logic for adding fetched checksums to an array request. Checksums are appropriately copied or created depending on extent layout. | src/object/tests/srv_checksum_tests.c |
| daos_test | daos_obj_update/fetch with checksums enabled. The -z flag can be used for specific checksum tests. Also --csum_type flag can be used to enable checksums with any of the other daos_tests | src/tests/suite/daos_checksum.c |

## Running Tests
**With daos_server not running**

```
./commont_test
./vos_test -z
./srv_checksum_tests
```
**With daos_server running**
```
export DAOS_CSUM_TEST_ALL_TYPE=1
./daos_server -z
./daos_server -i --csum_type crc64
```

---

# Checksum Scrubbing (In Development)
- [ ] How to throttle
- [ ] Decide where to put new counters (network corruption, general corruption)

A background task will scan (when the storage server is idle to limit
performance impact) the Version Object Store (VOS) trees to verify the data
integrity with the checksums. Corrective actions can be taken When corruption is
detected. See [Corrective Actions](#corrective-actions)

## Scanner Schedule
### Goals/Requirements
- Do not impact I/O performance. Two aspects here. 1- Don't use too much CPU
  during checksum calculations. 2- Don't use too much SSD bandwidth. Be able to
  throttle both. 
- Do not access media too frequently because it can cause it to wear out too
  quickly. 
- Be continuous instead of running on a schedule. Once complete immediately
  start over. Throttling approaches should prevent from scrubbing same objects
  too frequently.
  
 
### Design
- Per Pool ULT (I/O xstream) that will scan the containers and object tree. If a
  record value (SV or array) is visible and not marked corrupted ... (**??
  Confirm record visibility info from iteration entry.**)
  - Fetch data (vos_obj_fetch)
  - Create new ULT (helper xstream) to calculate checksum for data and verify
    with stored checksum. For each checksum that is calculated decrement
    "verification credits". If 0 is reached, yield.
  - If corruption is found by verification ULT, update record as corrupted
- For each iteration decrement "I/O credits", If 0 is reached, yield. 
- (**?? Are credits enough to throttle and prevent excesive media access?**)


## VOS Changes > src/vos/README.md
- Add flags to bio_addr_t and add CORRUPTED flag.
- On fetch, if value is already marked corrupted, return -DER_CSUM
- To mark a value as corrupted, use new flag in vos_obj_update (VOS_OF_CORRUPT)

## Object Changes > src/object/README.md
- When corruption is detected on the server during a fetch, aggregation, or
  rebuild the server calls VOS to update value as corrupted. 
- Add Server side verifying on fetch so can know if media or network corruption
  (note: need something so extents aren't double verified?) 
- New function to Server Object Checksum that will iterate all objects within a
  container to do actual scrubbing.

## Pool Changes > src/pool/README.md
- Add scrubbing ULT 
- Iterate containers (vos_iterate), open container, iterate objects recursively
  (vos_iterate) and call the Server Object Checksum function to scann the
  objects in the container. Provide a corruption handler so can initiate in
  place data repair or SSD eviction based on container configuration.
- See [Scanner Schedule](#scanner-schedule) for considerations on how to prevent
  the scrubber from impacting performance or reading the SSD too frequently. 

## Corrective Actions
There are two main options for corrective actions when data corruption is
discovered, in place data repair and SSD eviction. 

### In Place Data Repair
If enabled, when corruption is detected, the value identifier (dkey, akey, recx)
will be placed in a queue. When there are available cycles, the value identifier
will be used to request the data from a replica if exists and rewrite the data
locally. This will continue until the SSD Eviction threshold is reached, in
which case, the SSD is assumed to be bad enough that it isn't worth fixing
locally and it will be requested t obe evicted.

### SSD Eviction 
If enabled, when the SSD Eviction Threshold is reached the SSD will be evicted.
Current eviction methods are pool and target based so there will need to be a
mapping and mechanism in place to evict an SSD. When an SSD is evicted, the
rebuild protocol will be invoked.

Also, once the SSD Eviction Threshold is reached, the scanner should quit
scanning anything on that SSD.

## Additional Checksum Properties > doc/user/container.md
These properties are provided when a container is created, but should also be 
able to update them. When updated, they should be active right away.
- Scanner Interval - Minimum number of days scanning will take. Could take
  longer, but if only a few records will pad so takes longer. 
- Scanner Credits - number of checksums to process before yielding
- Disable scrubbing - at container level & pool level
- Threshold for when to evict SSD (% of device is marked as
  corrupted, number of corruption events, ??)
- In Place Correction - If the number checksum errors is below the Eviction
  Threshold, DAOS will attempt to repair the corrupted data using replicas if
  they exist.

**Not directly related to scanner**
- Server verify on **fetch**