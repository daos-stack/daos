# Container Management

DAOS containers are the unit of data management for users.

## Container Creation/Destroy

Containers can be created and destroyed through the daos_cont_create/destroy()
functions exported by the DAOS API. A user tool called `daos` is also
provided to manage containers.

To create a container:
```bash
$ daos cont create --pool=a171434a-05a5-4671-8fe2-615aa0d05094 --svc=0
Successfully created container 008123fc-6b6c-4768-a88a-a2a5ef34a1a2
```

The container type (i.e., POSIX or HDF5) can be passed via the --type option.
As shown below, the pool UUID, container UUID, and container attributes can be
stored in the extended attributes of a POSIX file or directory for convenience.
Then subsequent invocations of the daos tools need to reference the path
to the POSIX file or directory.

```bash
$ daos cont create --pool=a171434a-05a5-4671-8fe2-615aa0d05094 \
      --svc=0 --path=/tmp/mycontainer --type=POSIX --oclass=large \
      --chunk_size=4K
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

## Container Properties

At creation time, a list of container properties can be specified:

| **Container Property**     | **Description** |
| -------------------------  | --------------- |
| `DAOS_PROP_CO_LABEL`<img width=400/>| A string that a user can associate with a container. e.g., "Cat Pics" or "ResNet-50 training data"|
| `DAOS_PROP_CO_LAYOUT_TYPE` | The container type (POSIX, MPI-IO, HDF5, ...)|
| `DAOS_PROP_CO_LAYOUT_VER`  | A version of the layout that can be used by I/O middleware and application to handle interoperability.|
| `DAOS_PROP_CO_REDUN_FAC`   | The redundancy factor that drives the minimal data protection required for objects stored in the container. e.g., RF1 means no data protection, RF3 only allows 3-way replication or erasure code N+2.|
| `DAOS_PROP_CO_REDUN_LVL`   | The fault domain level that should be used to place data redundancy information (e.g., storage nodes, racks...). This information will be eventually consumed to determine object placement.|

While those properties are currently stored persistently with container
metadata, many of them are still under development. The ability to modify some
of these properties on an existing container will also be provided in a future
release.

## Data Integrity

Checksum configuration is done per container and is disabled by default.
To enable and configure checksums, the following container properties are used
during container create.

- `DAOS_PROP_CO_CSUM`: Type of checksum algorithm to use. Supported values are

```c
  DAOS_PROP_CO_CSUM_OFF, // default
  DAOS_PROP_CO_CSUM_CRC16,
  DAOS_PROP_CO_CSUM_CRC32,
  DAOS_PROP_CO_CSUM_CRC64,
```

- `DAOS_PROP_CO_CSUM_CHUNK_SIZE`: defines the chunk size used for
  creating checksums of array types. (default is 32K).
- `DAOS_PROP_CO_CSUM_SERVER_VERIFY`: Because of the probable decrease to
  IOPS, in most cases, it is not desired to verify checksums on an object
  update on the server side. It is sufficient for the client to verify on
  a fetch because any data corruption, whether on the object update,
  storage, or fetch, will be caught. However, there is an advantage to
  knowing if corruption happens on an update. The update would fail
  right away, indicating to the client to retry the RPC or report an
  error to upper levels.

!!! note
    Note that currently, once a container is created, its checksum configuration
    cannot be changed.

## Snapshot & Rollback

Similar to container create/destroy, a container can be snapshotted through the
DAOS API by calling daos_cont_create_snap(). Additional functions are provided
to destroy and list container snapshots.

The API also provides the ability to subscribe to container snapshot events and
to rollback the content of a container to a previous snapshot, but those
operations are not yet fully implemented.

This section will be updated once support for container snapshot is supported by
the `daos` tool.

The `DAOS_PROP_CO_SNAPSHOT_MAX` property is used to limit the maximum number of
snapshots to retain. When a new snapshot is taken, and the threshold is reached,
the oldest snapshot will be automatically deleted.

Rolling back the content of a container to a snapshot is planned for future DAOS
versions.

## User Attributes

Similar to POSIX extended attributes, users can attach some metadata to each
container through the daos_cont_{list/get/set}_attr() API.

## Access Control Lists

Client user and group access for containers is controlled by
[Access Control Lists (ACLs)](https://daos-stack.github.io/overview/security/#access-control-lists).

Access-controlled container accesses include:

* Opening the container for access.

* Reading and writing data in the container.

  * Reading and writing objects.

  * Getting, setting, and listing user attributes.

  * Getting, setting, and listing snapshots.

* Deleting the container (if the pool does not grant the user permission).

* Getting and setting container properties.

* Getting and modifying the container ACL.

* Modifying the container's owner.


This is reflected in the set of supported
[container permissions](https://daos-stack.github.io/overview/security/#permissions).

### Pool vs. Container Permissions

In general, pool permissions are separate from container permissions, and access
to one does not guarantee access to the other. However, a user must have
permission to connect to a container's pool before they can access the
container in any way, regardless of their permissions on that container.
Once the user has connected to a pool, container access decisions are based on
the individual container ACL. A user need not have read/write access to a pool
in order to open a container with read/write access, for example.

There is one situation in which the pool can grant a container-level permission:
Container deletion. If a user has Delete permission on a pool, this grants them
the ability to delete *any* container in the pool, regardless of their
permissions on that container.

If the user does not have Delete permission on the pool, they will only be able
to delete containers for which they have been explicitly granted Delete
permission in the container's ACL.

### Creating Containers with Custom ACL

To create a container with a custom ACL:

```bash
$ daos cont create --pool=<UUID> --svc=<rank> --acl-file=<path>
```

The ACL file format is detailed in the [ACL section](https://daos-stack.github.io/overview/security/#acl-file).

### Displaying a Container's ACL

To view a container's ACL:

```bash
$ daos cont get-acl --pool=<UUID> --svc=<rank> --cont=<UUID>
```

The output is in the same string format used in the ACL file during creation,
with one ACE per line.

### Modifying a Container's ACL

For all of these commands using an ACL file, the ACL file must be in the format
noted above for container creation.

#### Overwriting the ACL

To replace a container's ACL with a new ACL:

```bash
$ daos cont overwrite-acl --pool=<UUID> --svc=<rank> --cont=<UUID> \
      --acl-file=<path>
```

#### Adding and Updating ACEs

To add or update multiple entries in an existing container ACL:

```bash
$ daos cont update-acl --pool=<UUID> --svc=<rank> --cont=<UUID> \
      --acl-file=<path>
```

To add or update a single entry in an existing container ACL:

```bash
$ daos cont update-acl --pool=<UUID> --svc=<rank> --cont=<UUID> --entry <ACE>
```

If there is no existing entry for the principal in the ACL, the new entry is
added to the ACL. If there is already an entry for the principal, that entry
is replaced with the new one.

#### Removing an ACE

To delete an entry for a given principal in an existing container ACL:

```bash
$ daos cont delete-acl --pool=<UUID> --svc=<rank> --cont=<UUID> \
      --principal=<principal>
```

The principal corresponds to the principal portion of an ACE that was
set during container creation or a previous container ACL operation. For the
delete operation, the principal argument must be formatted as follows:

* Named user: `u:username@`
* Named group: `g:groupname@`
* Special principals:
  * `OWNER@`
  * `GROUP@`
  * `EVERYONE@`

The entry for that principal will be completely removed. This does not always
mean that the principal will have no access. Rather, their access to the
container will be decided based on the remaining ACL rules.

### Ownership

The ownership of the container corresponds to the special principals `OWNER@`
and `GROUP@` in the ACL. These values are a part of the container properties.
They may be set on container creation and changed later.

The owner-user (`OWNER@`) always has set-ACL and get-ACL permissions, even if
they are not explicitly granted by the ACL. This applies regardless of the other
permissions they are granted by ACE(s) in the ACL.

The owner-group (`GROUP@`) has no special permissions outside what they are
granted by the ACL.

#### Creating Containers with Specific Ownership

The default owner user and group are the effective user and group of the user
creating the container. However, a specific user and/or group may be specified
at container creation time.

```bash
$ daos cont create --pool=<UUID> --svc=<rank> --user=<owner-user> \
      --group=<owner-group>
```

The user and group names are case sensitive and must be formatted as
[DAOS ACL user/group principals](https://daos-stack.github.io/overview/security/#principal).

#### Changing Ownership

To change the owner user:

```bash
$ daos cont set-owner --pool=<UUID> --svc=<rank> --cont=<UUID> \
      --user=<owner-user>
```

To change the owner group:

```bash
$ daos cont set-owner --pool=<UUID> --svc=<rank> --cont=<UUID> \
      --group=<owner-group>
```

The user and group names are case sensitive and must be formatted as
[DAOS ACL user/group principals](https://daos-stack.github.io/overview/security/#principal).

## Compression & Encryption

The `DAOS_PROP_CO_COMPRESS` and `DAOS_PROP_CO_ENCRYPT` properties are reserved
for configuring respectively online compression and encryption.
These features are currently not on the roadmap.
