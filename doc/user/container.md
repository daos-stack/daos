# Container Management

DAOS containers are the unit of data management for users.

### Container Creation/Destroy

Containers can be created and destroyed through the daos_cont_create/destroy()
functions exported by the DAOS API. A user tool called `daos` is also
provided to manage containers.

To create a container:
```bash
$ daos container create --pool=a171434a-05a5-4671-8fe2-615aa0d05094 --svc=0
Successfully created container 008123fc-6b6c-4768-a88a-a2a5ef34a1a2
```

The container type (i.e., POSIX or HDF5) can be passed via the --type option.
As shown below, the pool UUID, container UUID, and container attributes can be
stored in the extended attributes of a POSIX file or directory for convenience.
Then subsequent invocations of the daos tools need to reference the path
to the POSIX file or directory.

```bash
$ daos container create --pool=a171434a-05a5-4671-8fe2-615aa0d05094 --svc=0 --path=/tmp/mycontainer --type=POSIX --oclass=large --chunk_size=4K
Successfully created container 419b7562-5bb8-453f-bd52-917c8f5d80d1 type POSIX
$ daos container query --svc=0 --path=/tmp/mycontainer
Pool UUID:      a171434a-05a5-4671-8fe2-615aa0d05094
Container UUID: 419b7562-5bb8-453f-bd52-917c8f5d80d1
Number of snapshots: 0
Latest Persistent Snapshot: 0
DAOS Unified Namespace Attributes on path /tmp/mycontainer:
Container Type: POSIX
Object Class:   large
Chunk Size:     4096
```

### Container Properties

At creation time, a list of container properties can be specified:

-   DAOS_PROP_CO_LABEL is a string that a user can associate with a
    container. e.g., "Cat Pics" or "ResNet-50 training data"

-   DAOS_PROP_CO_LAYOUT_TYPE is the container type (POSIX, MPI-IO,
    HDF5, ...)

-   DAOS_PROP_CO_LAYOUT_VER is a version of the layout that can be
    used by I/O middleware and application to handle interoperability.

-   DAOS_PROP_CO_CSUM defines whether checksums are enabled or
    disabled and the checksum type used. Available values are:
    - DAOS_PROP_CO_CSUM_OFF (Default)
    - DAOS_PROP_CO_CSUM_CRC16
    - DAOS_PROP_CO_CSUM_CRC32
    - DAOS_PROP_CO_CSUM_CRC64

-   DAOS_PROP_CO_CSUM_CHUNK_SIZE defines the chunk size used for
    creating checksums of array types. (default is 32K)

-   DAOS_PROP_CO_CSUM_SERVER_VERIFY is used to enable/disable the server
    verifying data with checksums on an object update. (default is
    disabled)

-   DAOS_PROP_CO_REDUN_FAC is the redundancy factor that drives the
    minimal data protection required for objects stored in the
    container. e.g., RF1 means no data protection, RF3 only allows 3-way
    replication or erasure code N+2.

-   DAOS_PROP_CO_REDUN_LVL is the fault domain level that should be
    used to place data redundancy information (e.g., storage nodes, racks
    ...). This information will be eventually consumed to determine object
    placement.

-   DAOS_PROP_CO_SNAPSHOT_MAX is the maximum number of snapshots to
    retain. When a new snapshot is taken, and the threshold is reached,
    the oldest snapshot will be automatically deleted.

-   DAOS_PROP_CO_ACL is the list of ACL for the container.

-   DAOS_PROP_CO_COMPRESS and DAOS_PROP_CO_ENCRYPT are reserved for configuring
 respectively compression and encryption. These features
    are currently not on the roadmap.

While those properties are currently stored persistently with container
metadata, many of them are still under development. The ability to modify some
of these properties on an existing container will also be provided in a future release.

### Container Snapshot

Similar to container create/destroy, a container can be snapshotted through the
DAOS API by calling daos_cont_create_snap(). Additional functions are provided
to destroy and list container snapshots.

The API also provides the ability to subscribe to container snapshot events and
to rollback the content of a container to a previous snapshot, but those
operations are not yet fully implemented.

This section will be updated once support for container snapshot is supported by
the `daos` tool.

### Container User Attributes

Similar to POSIX extended attributes, users can attach some metadata to each
container through the daos_cont_{list/get/set}_attr() API.

### Container ACLs

Support for per-container ACLs is scheduled for DAOS v1.2. Similar to pool ACLs,
container ACLs will implement a subset of the NFSv4 ACL standard. This feature
will be documented here once available.
