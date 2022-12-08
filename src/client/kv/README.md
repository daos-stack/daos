# DAOS KV API

The KV API simplifies the DAOS Object API and exposes a simple API to manipulate
a Key-Value object with simple put/get/remove/list operations. The API exposes
only a single Key (no multi-level keys) and a value associated with that key
which is overwritten entirely anytime the key is updated. So internally the
mapping of the Multi-level KV object looks like:

~~~~~~
Key -> DKey
NULL AKEY
Value -> Single Value
~~~~~~

The API is currently tested with daos_test.
