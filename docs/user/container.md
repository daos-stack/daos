# Container Management

DAOS containers are datasets managed by the users. Similarly to S3 buckets,
a DAOS container is a collection of objects that can be presented to applications
through different interfaces including POSIX I/O (files and directories),
HDF5, SQL or any other data models of your choice.

A container belongs to a single pool and shares the space with other containers.
It is identified by a label selected by the user and an immutable UUID allocated
when the container is first created.

The container is the unit of data management in DAOS and can be snapshotted or
cloned.

!!! warning
    DAOS containers are storage containers and should not be confused with Linux
    containers.

## Container Basics

Containers can be created, queried, relabeled, listed and destroyed through the
`daos(1)` utility or native DAOS API.

### Creating a Container

To create and then query a container labeled `mycont` on a pool
labeled `tank`:

```bash
$ daos cont create tank mycont
  Container UUID : daefe12c-45d4-44f7-8e56-995d02549041
  Container Label: mycont
  Container Type : unknown
Successfully created container daefe12c-45d4-44f7-8e56-995d02549041

$ daos cont query tank mycont
  Container UUID             : daefe12c-45d4-44f7-8e56-995d02549041
  Container Label            : mycont
  Container Type             : unknown
  Pool UUID                  : 0d1fad71-5681-48d4-acdd-7bb2e786f12e
  Number of snapshots        : 0
  Latest Persistent Snapshot : 0
  Highest Aggregated Epoch   : 263546931609567249
  Container redundancy factor: 0
  Snapshot Epochs            :
```

Like pools, container
labels can be up to 127 characters long and must only include alphanumeric
characters, colon (':'), period ('.'), hyphen ('-') or underscore ('\_').
Labels that can be parsed as UUID are not allowed.

The container type (i.e., POSIX or HDF5) can be passed via the --type option.

For convenience, a container can also be identified by a path to a filesystem
supporting extended attributes. In this case, the pool and container UUIDs
are stored in an extended attribute of the target file or directory that can
then be used in subsequent command invocations to identify the container.

```bash
$ daos cont create tank mycont --path /tmp/mycontainer --type POSIX --oclass=SX
  Container UUID : 30e5d364-62c9-4ddf-9284-1021359455f2
  Container Type : POSIX

Successfully created container 30e5d364-62c9-4ddf-9284-1021359455f2 type POSIX

$ daos cont query --path /tmp/mycontainer
  Container UUID             : 30e5d364-62c9-4ddf-9284-1021359455f2
  Container Type             : POSIX
  Pool UUID                  : 0d1fad71-5681-48d4-acdd-7bb2e786f12e
  Number of snapshots        : 0
  Latest Persistent Snapshot : 0
  Highest Aggregated Epoch   : 263548861715283973
  Container redundancy factor: 0
  Snapshot Epochs            :
  Object Class               : SX
  Chunk Size                 : 1.0 MiB
```

By default, containers are created without data protection enabled. This can
be modified by changing the redundancy factor (rf) property at creation time.
To create a container that can support one engine failure, use a redundancy
factor of 1 as follows:

```bash
$ daos cont create tank mycont1 --type POSIX --properties rd_fac:1
  Container UUID : b396e2ca-2077-4908-9ff2-1af4b4b2fd4a
  Container Label: mycont1
  Container Type : unknown
Successfully created container b396e2ca-2077-4908-9ff2-1af4b4b2fd4a
```

Please refer to the [redundancy factor property](#Redundancy_Factor)
for more information.

### Listing Containers

To list all containers available in a pool:

```bash
$ daos cont list tank
UUID                                 Label
----                                 -----
30e5d364-62c9-4ddf-9284-1021359455f2 container_label_not_set
daefe12c-45d4-44f7-8e56-995d02549041 mycont
```

### Destroying a Container

To destroy a container:
```bash
$ daos cont destroy tank mycont
Successfully destroyed container mycont

$ daos cont destroy --path /tmp/mycontainer
Successfully destroyed container 30e5d364-62c9-4ddf-9284-1021359455f2
```

If the container is in use, the force option (i.e., --force or -f) must be added.
Active users of a force-deleted container will fail with a bad handle error.

!!! tip
    It is orders of magniture faster to destroy a container compared to
    punching/deleting each object individually.

## Container Properties

Container properties are the main mechanism that one can use to control the
behavior of containers. This includes the type of middleware, whether some
features like deduplication or checksum are enabled. Some properties are
immutable after creation creation, while some others can be dynamically
changed.

### Listing Properties

The `daos` utility may be used to list all container's properties as follows:

```bash
$ daos cont get-prop tank mycont
# -OR- --path interface shown below
$ daos cont get-prop --path=/tmp/mycontainer
Properties for container mycont
Name                  Value
----                  -----
Highest Allocated OID 0
Checksum              off
Checksum Chunk Size   32 KiB
Compression           off
Deduplication         off
Dedupe Threshold      4.0 KiB
EC Cell Size          64 kiB
Encryption            off
Group                 jlombard@
Label                 mycont
Layout Type           unknown (0)
Layout Version        1
Max Snapshot          0
Owner                 jlombard@
Redundancy Factor     rd_fac0
Redundancy Level      rank (1)
Server Checksumming   off
Health                HEALTHY
Access Control List   A::OWNER@:rwdtTaAo, A:G:GROUP@:rwtT
```

Additionally, a container's properties may be retrieved using the
libdaos API `daos_cont_query()` function. Refer to the file
src/include/daos\_cont.h Doxygen comments and the online documentation
available [here](https://docs.daos.io/v2.6/doxygen/html/).

### Changing Properties

By default, a container will inherit a set of default value for each property.
Those can be overridden at container creation time via the `--properties` option.

```bash
$ daos cont create tank mycont2 --properties cksum:sha1,dedup:hash,rd_fac:1
  Container UUID : a6286ead-1952-4faa-bf87-00fc0f3785aa
  Container Label: mycont2
  Container Type : unknown
Successfully created container a6286ead-1952-4faa-bf87-00fc0f3785aa

$ daos cont query tank mycont2
Properties for container mycont2
Name                  Value
----                  -----
Highest Allocated OID 0
Checksum              sha1
Checksum Chunk Size   32 KiB
Compression           off
Deduplication         hash
Dedupe Threshold      4.0 KiB
EC Cell Size          64kiB
Encryption            off
Group                 jlombard@
Label                 mycont2
Layout Type           unknown (0)
Layout Version        1
Max Snapshot          0
Owner                 jlombard@
Redundancy Factor     rd_fac1
Redundancy Level      rank (1)
Server Checksumming   off
Health                HEALTHY
Access Control List   A::OWNER@:rwdtTaAo, A:G:GROUP@:rwtT
```

Mutable properties can be modified after container creation via the `set-prop`
option.

```bash
$ daos cont set-prop tank mycont2 --properties label:mycont3
Properties were successfully set
```

This effectively changed the container label.

```bash
$ daos cont get-prop tank mycont2
ERROR: daos: DER_NONEXIST(-1005): The specified entity does not exist

$ daos cont get-prop tank mycont3
Properties for container mycont3
Name                  Value
----                  -----
Highest Allocated OID 0
Checksum              sha1
Checksum Chunk Size   32 KiB
Compression           off
Deduplication         hash
Dedupe Threshold      4.0 KiB
EC Cell Size          64 kiB
Encryption            off
Group                 jlombard@
Label                 mycont3
Layout Type           unknown (0)
Layout Version        1
Max Snapshot          0
Owner                 jlombard@
Redundancy Factor     rd_fac1
Redundancy Level      rank (1)
Server Checksumming   off
Health                HEALTHY
Access Control List   A::OWNER@:rwdtTaAo, A:G:GROUP@:rwtT
```

### Property Values

The table below summarizes the available container properties.

| **Container Property**  | **Immutable**   | **Description** |
| ------------------------| --------------- | ----------------|
| label			  | No              | String associate with a containers. e.g., "Cat\_Pics" or "training\_data"|
| owner                   | Yes             | User acting as the owner of the container|
| group                   | Yes             | Group acting as the owner of the container|
| acl                     | No              | Container access control list|
| layout\_type            | Yes             | Container type (e.g., POSIX, HDF5, ...)|
| layout\_ver             | Yes             | Layout version to be used at the discretion of I/O middleware for interoperability|
| rd\_fac                 | Yes             | Redundancy Factor which is the maximum number of simultaneous engine failures that objects can support without data loss|
| rd\_lvl                 | Yes             | Redundancy Level which is the level in the fault domain hierarchy to use for object placement|
| health                  | No              | Current state of the container|
| alloc\_oid              | No              | Maximum allocated object ID by container allocator|
| ec\_cell\_sz            | Yes             | Erasure code cell size for erasure-coded objects|
| cksum                   | Yes             | Checksum off, or algorithm to use (adler32, crc[16,32,64] or sha[1,256,512])|
| cksum\_size             | Yes             | Checksum Size determining the maximum extent size that a checksum can cover|
| srv\_cksum              | Yes             | Whether to verify checksum on the server before writing data (default: off)|


Moreover, the following properties have been added as placeholders, but are not
fully supported yet:

| **Container Property**  | **Immutable**   | **Description** |
| ------------------------| --------------- | ----------------|
| max\_snapshot           | No              | Impose a upper limit on number of snapshots to retain (default: 0, no limitation)|
| compression             | Yes             | Online compression off, or algorithm to use (off, lz4, deflate[1-4])|
| dedup                   | Yes             | Inline deduplication off, or algorithm to use (hash or memcmp)|
| dedup\_threshold        | Yes             | Minimum I/O size to consider for deduplication|
| encryption              | Yes             | Inline encryption off, or algorithm to use (XTS[128,256], CBC[128,192,256] or GCM[128,256])|

Please refer to the next sections for more details on each property.

### Container Type

A DAOS container type denotes a specific storage middleware, which implements
its own data layout on top of the main DAOS API that is provided through `libdaos`.
Container types are specified through the immutable `layout_type` container property.
Within each container type, the `layout_version` property provides a mechanism for
versioning - usage of this version number is determined by the respective middleware.

Some container layouts are defined as part of the main DAOS project.
The best-known example is the `POSIX` container type that is used to implement the
[DAOS File System (DFS)](filesystem.md) layout with files and directories.
Other container layouts are created by various user communities that are
implementing their own domain-specific storage middleware on top of DAOS.

The known DAOS container types are maintained as an enumerated list in the
[`daos_prop.h`](https://github.com/daos-stack/daos/blob/master/src/include/daos_prop.h#L284)
header file. The following container types are currently defined,
and can be used with the `daos cont create --type` command option:

| **Container Type** | **Description** |
| ------------------ | --------------- |
| UNKNOWN            | No container type was specified at container create time, or the specified container type is unknown. |
| POSIX              | [DAOS Filesystem (DFS)](filesystem.md), also used with dfuse and by the [MPI-IO DAOS backend](mpi-io.md). |
| HDF5               | [HDF5 DAOS VOL connector](hdf5.md), maintained by [The HDF Group](https://www.hdfgroup.org/?s=DAOS). |
| PYTHON             | [PyDAOS](python.md) container format. |
| SPARK              | A specific layout for [Apache Spark](spark.md) shuffle. |
| DATABASE           | SQL Database, used by an experimental DAOS interface to MariaDB. |
| ROOT               | ROOT/RNTuple format, maintained by [CERN](https://root.cern.ch/). |
| SEISMIC            | DAOS Seismic Graph, aka SEG-Y, maintained by the [segy-daos](https://github.com/daos-stack/segy-daos) project. |
| METEO              | Meteorology, aka Fields Database (FDB), maintained by [ECMWF](https://www.ecmwf.int/search/site/FDB). |

To register a new DAOS container type (represented as an integer number and a
corresponding `DAOS_PROP_CO_LAYOUT_*` mnemonic name for that integer in the
`daos_prop.h` header), please get in touch with the DAOS engineering team.

### Redundancy Factor

Objects in a DAOS container may belong to different object classes and
have different levels of data protection. While this model gives a lot of control
to the users, it also requires carefully selecting a suitable class for each
object. If objects with different data protection level are also stored in the
same container, the user should also be prepared for the case where some objects
might suffer from data loss after several cascading failures, while some others
with higher level of data protection may not. This incurs extra complexity that
not all I/O middleware necessarily wants to deal with.

To lower the bar of adoption while still keeping the flexibility, two container
properties have been introduced:

- the redundancy factor (rd\_fac) that describes the number of concurrent engine
  exclusions that objects in the container are protected against. The rd\_fac value
  is an integer between 0 (no data protection) and 5 (support up to 5
  simultaneous failures).
- a `health` property representing whether any object content might have been
  lost due to cascading engine failures. The value of this property can be
  either `HEALTHY` (no data loss) or `UNCLEAN` (data might have been lost).

The redundancy factor can be set at container creation time and cannot be
modified after creation.

```bash
$ daos cont create tank mycont1 --type POSIX --properties rd_fac:1
  Container UUID : b396e2ca-2077-4908-9ff2-1af4b4b2fd4a
  Container Label: mycont1
  Container Type : unknown
Successfully created container b396e2ca-2077-4908-9ff2-1af4b4b2fd4a
```

It can be checked by listing the properties:

```bash
$ daos cont get-prop tank mycont1
Properties for container mycont1
Name                  Value
----                  -----
[...]
Redundancy Factor     rd_fac1
Redundancy Level      rank (1)
Health                HEALTHY
[...]
```

Only objects with data protection enabled can be stored in such a container.
This includes replicated or erasure-coded objects. Attempts to open an
object with a class that does not support data redundancy (e.g., SX)
will fail.

For redundancy factor 2, only objects with at least 3-way replication or erasure
code with two parities or more can be stored in the container.

As long as the number of simultaneous engine failures is below the redundancy
factor, the container is reported as healthy. if not, then the container is
marked as unclean and cannot be accessed.

```bash
$ daos cont get-prop tank mycont1
Properties for container mycont1
Name                  Value
----                  -----
[...]
Redundancy Factor     rd_fac1
Redundancy Level      rank (1)
Health                UNCLEAN
[...]
```

For instance, an attempt to mount with dfuse this POSIX container fails as
follows:

```bash
$ dfuse --pool tank --container mycont1 -m /tmp/dfuse
dfuse ERR  src/client/dfuse/dfuse_core.c:873 dfuse_cont_open(0x19b9b00) daos_cont_open() failed: DER_RF(-2031): 'Failures exceed RF'
Failed to connect to container (5) Input/output error
```

If the excluded engines can be reintegrated in the pool by the administrator,
then the container state will automatically switch back to healthy and can be
accessed again.

If the user is willing to access an unhealthy container (e.g., to recover data),
the force flag can be passed on container open or the container state can be
forced to healthy via `daos cont set-prop tank mycont1 --properties status:healthy`.

The redundancy level (rd\_lvl) is another property that was introduced to
specify the fault domain level to use for placement.

### Data Integrity

DAOS allows to detect and fix (when data protection is enabled) silent data
corruptions. This is done by calculating checksums for both data and metadata
in the DAOS library on the client side and storing those checksums persistently
in SCM. The checksums will then be validated on access and on update/write as
well on the server side if server verify option is enabled.

Corrupted data will never be returned to the application. When a corruption is
detected, DAOS will try to read from a different replica, if any.  If the
original data cannot be recovered, then an error will be reported to the
application.

To enable and configure checksums, the following container properties are used
during container create.

- cksum (`DAOS_PROP_CO_CSUM`): the type of checksum algorithm to use.
  Supported values are adler32, crc[16|32|64] or sha[1|256|512]. By default,
  checksum is disabled for new containers.
- cksum\_size (`DAOS_PROP_CO_CSUM_CHUNK_SIZE`): defines the chunk size used for
  creating checksums of array types. (default is 32K).
- srv\_cksum (`DAOS_PROP_CO_CSUM_SERVER_VERIFY`): Because of the probable decrease to
  IOPS, in most cases, it is not desired to verify checksums on an object
  update on the server side. It is sufficient for the client to verify on
  a fetch because any data corruption, whether on the object update,
  storage, or fetch, will be caught. However, there is an advantage to
  knowing if corruption happens on an update. The update would fail
  right away, indicating to the client to retry the RPC or report an
  error to upper levels.

For instance, to create a new container with crc64 checksum enabled and
checksum verification on the server side, one can use the following command
line:

```bash
$ daos cont create tank mycont --properties cksum:crc64,srv_cksum:on
Successfully created container dfa09efd-4529-482c-b7cd-748c29ef7419

$ daos cont get-prop  tank mycont4 | grep cksum
Checksum              crc64
Checksum Chunk Size   32 KiB
Server Checksumming   on
```

!!! note
    Note that currently, once a container is created, its checksum configuration
    cannot be changed.

### Erasure Code

The DAOS erasure code implementation uses a fixed cell size that applies to all
objects in the container.
The cell size in DAOS is the size of a single data and parity fragment.
By default, a container's `ec_cell_sz` property is inherited from the pool's
default `ec_cell_sz`, which was 1MiB in DAOS 2.0 and has been reduced to
64kiB in DAOS 2.2.  The container cell size can also be set at
container creation time via the `--property` option:

```bash
$ daos cont create tank mycont5 --type POSIX --properties rd_fac:1,cell_size:131072
  Container UUID : 90185799-0e22-4a0b-be9d-1a20900a35ee
  Container Label: mycont5
  Container Type : unknown
Successfully created container 90185799-0e22-4a0b-be9d-1a20900a35ee
```

This will set an EC cell size of 128 KiB for all erasure-coded objects created in
this container.

DFS (POSIX) containers use a default `chunk_size` of 1MiB.
This is the largest I/O request size that a DFS client will send to a storage target
in a single request. The `chunk_size` can be displayed with the `daos cont query` command.
When using Erasure Coding as the data protection mechanism, performance is best when
the _stripe width_ of an EC stripe is either identical to the container's `chunk size`
or an integer multiple of the _stripe width_ is equal to the container's `chunk_size`.
For example:

* With a DFS container chunk size of 1MiB, an `ec_cell_sz` of 128kiB is a perfect setting
  for EC\_8P1GX and EC\_8P2GX: Eight EC cells of 128kiB exactly match the 1MiB chunk size.
  It is also good for smaller erasure coding stripe widths like EC\_4P1GX and
  EC\_4P2GX: Four EC cells of 128kiB are 512kiB, half of the 1MiB chunk size.
  So a single DFS container chunk will fill two _full stripes_.

* With a DFS container chunk size of 1MiB, an `ec_cell_sz` of 128kiB is **not** a good
  fit for EC\_16P2GX and other more widely striped EC types: Sixteen EC cells of
  128kiB are 2MiB, twice as big as the DFS container's chunk size.
  This means that even the largest DFS client write operation results in a
  _read-modify-write_ penalty, because it only fills **half** of an EC stripe.

* With a DFS container chunk size of 1MiB, an `ec_cell_sz` of 64kiB is a perfect setting
  for EC\_16P1GX and EC\_16P2GX: Sixteen EC cells of 64kiB exactly match the 1MiB chunk size.
  Smaller EC stripe widths like EC\_8P2GX and EC\_4P1GX also work with this EC cell size,
  which is the reason why 64kiB is the new DAOS 2.2 default for the `ec_cell_sz`.


### Checksum Background Scrubbing

A pool ULT can be configured to scan the VOS trees to discover silent data
corruption proactively. (see data_integrity.md for more details). This can be
disabled per container using the `DAOS_PROP_CO_SCRUBBER_DISABLED` container
property.

### Deduplication (Preview)

Data deduplication (dedup) is a process that allows to eliminate duplicated
data copies in order to decrease capacity requirements. DAOS has some initial
support of inline dedup.

When dedup is enabled, each DAOS server maintains a per-pool table indexing
extents by their hash (i.e., checksum). Any new I/Os bigger than the
deduplication threshold will thus be looked up in this table to find out
whether an existing extent with the same signature has already been stored.
If an extent is found, then two options are provided:

- Transferring the data from the client to the server and doing a memory compare
  (i.e., memcmp) of the two extents to verify that they are indeed identical.
- Trusting the hash function and skipping the data transfer. To minimize issue
  with hash collision, a cryptographic hash function (i.e., SHA256) is used in
  this case. The benefit of this approarch is that the data to be written does
  not need to be transferred to the server. Data processing is thus greatly
  accelerated.

The inline dedup feature can be enabled on a per-container basis. To enable and
configure dedup, the following container properties are used:

- dedup (`DAOS_PROP_CO_DEDUP`): Type of dedup mechanism to use. Supported values
  are off (default), memcmp (memory compare) or hash (hash-based using SHA256).
- dedup\_threshold (`DAOS_PROP_CO_DEDUP_THRESHOLD`): defines the minimal I/O size
  to consider the I/O for dedup (default is 4K).

!!! warning
    Dedup is a feature preview in 2.0 and has some known
    limitations. Aggregation of deduplicated extents isn't supported and the
    checksum tree isn't persistent yet. This means that aggregation is disabled
    for a container with dedplication enabled and duplicated extents won't be
    matched after a server restart.
    NVMe isn't supported for dedup enabled container, so please make sure not
    using dedup on the pool with NVMe enabled.

### Compression (unsupported)

The compression (`DAOS_PROP_CO_COMPRESS`) property is reserved for configuring
online compression and not implemented yet.

### Encryption (unsupported)

The encryption (`DAOS_PROP_CO_ENCRYPT`) property is reserved for configuring
online encryption and not implemented yet.

## Snapshot & Rollback

The `daos` tool provides container {create/destroy}-snap and list-snaps
commands.

```bash
$ daos cont create-snap tank mycont
snapshot/epoch 262508437483290624 has been created

$ daos cont list-snaps tank mycont
Container's snapshots :
262508437483290624

$ daos cont destroy-snap tank mycont -e 262508437483290624
```

The max\_snapshot (`DAOS_PROP_CO_SNAPSHOT_MAX`) property is used to limit the
maximum number of snapshots to retain. When a new snapshot is taken, and the
threshold is reached, the oldest snapshot will be automatically deleted.

Rolling back the content of a container to a snapshot is planned for future
DAOS versions.

## User Attributes

Similar to POSIX extended attributes, users can attach some metadata to each
container through the `daos cont [set|get|list|del]-attr` commands or via the
`daos_cont_{list/get/set}_attr()` functions of the libdaos API.

```bash
$ daos cont set-attr tank mycont import_date "12/01/2021"

$ daos cont list-attr tank mycont
Attributes for container mycont:
Name
----
import_date

$ daos cont get-attr tank mycont import_date
Attributes for container mycont:
Name        Value
----        -----
import_date 12/01/2021

$ daos cont del-attr tank mycont import_date

$ daos cont list-attr tank mycont
Attributes for container mycont:
  No attributes found.
```

## Access Control Lists

Client user and group access for containers is controlled by
[Access Control Lists (ACLs)](https://docs.daos.io/v2.6/overview/security/#access-control-lists).

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
[container permissions](https://docs.daos.io/v2.6/overview/security/#permissions).

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

### ACL at Container Creation

To create a container labeled mycont in a pool labeled tank with a custom ACL:

```bash
$ export DAOS_POOL="tank"
$ export DAOS_CONT="mycont"
$ daos cont create $DAOS_POOL $DAOS_CONT --acl-file=<path>
```

The ACL file format is detailed in the
[security overview](https://docs.daos.io/v2.6/overview/security/#acl-file).

### Displaying ACL

To view a container's ACL:

```bash
$ daos cont get-acl $DAOS_POOL $DAOS_CONT
```

The output is in the same string format used in the ACL file during creation,
with one ACE per line.

### Modifying ACL

For all of these commands using an ACL file, the ACL file must be in the format
noted above for container creation.

#### Overwriting ACL

To replace a container's ACL with a new ACL:

```bash
$ daos cont overwrite-acl $DAOS_POOL $DAOS_CONT --acl-file=<path>
```

#### Adding and Updating ACEs

To add or update multiple entries in an existing container ACL:

```bash
$ daos cont update-acl $DAOS_POOL $DAOS_CONT --acl-file=<path>
```

To add or update a single entry in an existing container ACL:

```bash
$ daos cont update-acl $DAOS_POOL $DAOS_CONT --entry <ACE>
```

If there is no existing entry for the principal in the ACL, the new entry is
added to the ACL. If there is already an entry for the principal, that entry
is replaced with the new one.

#### Removing an ACE

To delete an entry for a given principal in an existing container ACL:

```bash
$ daos cont delete-acl $DAOS_POOL $DAOS_CONT --principal=<principal>
```

The `principal` argument refers to the
[principal](https://docs.daos.io/v2.6/overview/security/#principal), or
identity, of the entry to be removed.

For the delete operation, the `principal` argument must be formatted as follows:

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

#### Privileges

The owner-user (`OWNER@`) has some implicit privileges on their container.
These permissions are silently included alongside any permissions that the
user was explicitly granted by entries in the ACL.

The owner-user will always have the following implicit capabilities:

* Open container
* Set ACL (A)
* Get ACL (a)

Because the owner's special permissions are implicit, they do not need to be
specified in the `OWNER@` entry. After
[determining](https://docs.daos.io/v2.6/overview/security/#enforcement)
the user's privileges from the container ACL, DAOS checks whether the user
requesting access is the owner-user. If so, DAOS grants the owner's
implicit permissions to that user, in addition to any permissions granted by
the ACL.

In contrast, the owner-group (`GROUP@`) has no special permissions beyond those
explicitly granted by the `GROUP@` entry in the ACL.

#### Setting Ownership at Creation

The default owner user and group are the effective user and group of the user
creating the container. However, an owner-group may be specified at container
creation time.

```bash
$ daos cont create --group=<owner-group> $DAOS_POOL $DAOS_CONT
```

The group names are case sensitive and must be formatted as
[DAOS ACL group principals](https://docs.daos.io/v2.6/overview/security/#principal).

#### Changing Ownership

To change the owner user:

```bash
$ daos cont set-owner $DAOS_POOL $DAOS_CONT --user=<owner-user>
```

To change the owner group:

```bash
$ daos cont set-owner $DAOS_POOL $DAOS_CONT --group=<owner-group>
```

The user and group names are case sensitive and must be formatted as
[DAOS ACL user/group principals](https://docs.daos.io/v2.6/overview/security/#principal).
