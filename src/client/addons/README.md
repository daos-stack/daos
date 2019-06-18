# DAOS Addons

The DAOS addons constitute APIs that are built on top of the DAOS API. The
purpose is to provide some simplified or additional functionality that can be
achieved with the current API and would be commonly used by several applications
and middleware libraries to warrant a common implementation within the DAOS
library to avoid multiple implementations externally.

Currently the DAOS addons include:
- An Array object and API built on top of the DAOS Key-Array object.
- A simplified DAOS Key Value API.

## DAOS Arrays

A DAOS Array is a special DAOS object to expose a logical 1-dimentional array to
the user. The array is created by the user with an immutable record size and
chunk size. Additional APIs are provided to access the array (read, write,
punch).

### Array Representation

The Array representation over the DAOS KV API is done with integer typed DKeys,
where each DKey holds chunk_size records. Each DKey has 1 AKey with a NULL value
that holds the user array data in an array type extent. The only exception is
the first DKey (which is 0) that holds a special AKey to hold the array
metadata:

~~~~~~
DKey: 0
AKey: "daos_array_metadata"
Single Value: 3 uint64_t
       [0] = magic value (0xdaca55a9daca55a9)
       [1] = array cell size
       [2] = array chunk size
~~~~~~

To illustrate the array mapping, suppose we have a logical array of 10 elements
and chunk size being 3. The DAOS KV representation would be:

~~~~~~
DKey: 0
Array records: 0, 1, 2
DKey: 1
Array records: 3, 4, 5
DKey: 2
Array records: 6, 7, 8
DKey: 3
Array records: 9
~~~~~~

### API and Implementation

The API (include/daos_addons.h) includes operations to:
- create an array with the required, immutable metadata of the array.
- open an existing array which returns the metadata associated with the array.
- read from an array object.
- write to an array object.
- set size (truncate) of an array. Note this is not equivaluent to preallocation.
- get size of an array.
- punch a range of records from the array.
- destroy/remove an array.

The Array API is implemented using the DAOS Task API. For example, the read and
write operations create an I/O operation for each DKey and inserts them into the
task engine with a parent task that depends on all the child tasks that do the
I/O.

The API is currently tested with daos_addons_test.

## DAOS High Level (HL) API

The HL API simplifies the DAOS Object API and exposes a simple API to manipulate
a Key-Value object with simple put/get/remove/list operations. The API exposes
only a single Key (no multi-level keys) and a value associated with that key
which is overwritten entirely anytime the key is updated. So internally the
mapping of a the HL KV object looks like:

~~~~~~
Key -> DKey
NULL AKEY
Value -> Single Value
~~~~~~

The API is currently tested with daos_addons_test.
