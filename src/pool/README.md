# DAOS Pool

A pool is a set of targets spread across different storage nodes over which data and metadata are distributed to achieve horizontal scalability, and replicated or erasure-coded to ensure durability and availability (see: <a href="/docs/overview/storage.md#daos-pool">Storage Model: DAOS Pool</a>).

<a id="9.1"></a>

## Pool Service

The Pool Service (`pool_svc`) stores the metadata for pools, and provides an API to query and update the pool configuration. Pool metadata are organized as a hierarchy of key-value stores (KVS) that is replicated over a number of servers backed by Raft consensus protocol which uses strong leadership; client requests can only be serviced by the service leader while non-leader replicas merely respond with a hint pointing to the current leader for the client to retry. `pool_svc` derives from a generic replicated service module `rsvc` (see: <a href="/src/rsvc/README.md#architecture">Replicated Services: Architecture</a>) whose implementation facilitates the client search for the current leader.

<a id="9.1.1"></a>

#### Metadata Layout

![Pool Service Layout](/docs/graph/Fig_072.png "Pool Service Layout")

The top-level KVS stores the pool map, security attributes such as the UID, GID and mode, information related to space management and self-healing (see: <a href="/src/rebuild/README.md">Rebuild</a>) as well as a second-level KVS containing user-defined attributes (see: <a href="/src/container/README.md#metadata-layout">Container Service: Metadata Layout</a>). In addition, it also stores information on pool connections, represented by a pool handle and identified by a client-generated handle UUID. The terms "pool connection" and "pool handle" may be used interchangeably.

<a id="9.3"></a>

## Pool Operations

<a id="9.3.1"></a>

#### Pool / Pool Service Creation

Pool creation is driven entirely by the Management Service since it requires special privileges for steps related to allocation of storage and querying of fault domains. After formatting all the targets, the management module passes the control to the pool module by calling the`ds_pool_svc_create`, which initializes service replication on the selected subset of nodes for the combined Pool and Container Service. The Pool module now sends a `POOL_CREATE` request to the service leader which creates the service database; the list of targets and their fault domains are then converted into the initial version of the pool map and stored in the pool service, along with other initial pool metadata.

<a id="9.3.2"></a>

#### Pool Connection

To establish a pool connection, a client process calls the `daos_pool_connect` method in the client library with the pool UUID, connection information (such as group name and list of service ranks) and connection flags; this initiates a `POOL_CONNECT` request to the Pool Service. The Pool Service tries to authenticate the request according to the security model in use (e.g., UID/GID in a POSIX-like model), and to authorize the requested capabilities to the client-generated pool handle UUID.  Before proceeding, the pool map is transferred to the client; if there are errors from this point onwards, the server can simply ask the client to discard the pool map.

At this point, the Pool Service checks for existing pool handles:

- If a pool handle with the same UUID already exists, a pool connection has already been established and nothing else needs to be done.
- If another pool handle exists such that either the currently requested or the existing one has exclusive access, the connection request is rejected with a busy status code.

If everything goes well, the pool service sends a collective `POOL_TGT_CONNECT` request to all targets in the pool with the pool handle UUID. The Target Service creates and caches the local pool objects and opens the local VOS pool for access.

A group of peer application processes may share a single pool connection handle (see: <a href="/docs/overview/storage.md#daos-pool">Storage Model: DAOS Pool</a> and <a href="/docs/overview/use_cases.md#storage-management--workflow-integration">Use Cases: Storage Management and Workflow Integration</a>).

To close a pool connection, a client process calls the `daos_pool_disconnect` method in the client library with the pool handle, triggering a `POOL_DISCONNECT` request to the Pool Service, which sends a collective `POOL_TGT_DISCONNECT` request to all targets in the pool. These steps destroy all state associated with the connection, including all container handles. Other client processes sharing this connection should destroy their copies of the pool handle locally, preferably before the disconnect method is called on behalf of everyone. If a group of client processes terminate prematurely, before having a chance to call the pool disconnect method, their pool connection will eventually be evicted once the pool service learns about the event from the run-time environment.
