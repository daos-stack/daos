# Application Interface and Tiering

## DAOS Container Management

DAOS containers are the unit of data management for users.

### Container Creation/Destroy

Containers can be created and destroyed through the daos_cont_create/destroy()
functions exported by the DAOS API. A user tool called `daos` is also
provided to manage containers.

To create a container:
```
$ daos container create --pool=a171434a-05a5-4671-8fe2-615aa0d05094 --svc=0
Successfully created container 008123fc-6b6c-4768-a88a-a2a5ef34a1a2
```

The container type (i.e., POSIX or HDF5) can be passed via the --type option.
As shown below, the pool UUID, container UUID, and container attributes can be
stored in the extended attributes of a POSIX file or directory for convenience.
Then subsequent invocations of the daos tools need to reference the path
to the POSIX file or directory.

```
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

## Native Programming Interface

### Building against the DAOS library

To build application or I/O middleware against the native DAOS API, include
the daos.h header file in your program and link with -Ldaos. Examples are
available under src/tests.

### DAOS API Reference

libdaos is written in C and uses Doxygen comments that are added to C header
files.

\[TODO] Generate Doxygen document and add a link here.

### Bindings to Different Languages

API bindings to both Python[^1] and Go[^2] languages are available.

## POSIX Filesystem

A regular POSIX namespace can be encapsulated into a DAOS container.  This
capability is provided by the libdfs library that implements the file and
directory abstractions over the native libdaos library. The POSIX emulation can
be exposed to applications or I/O frameworks either directly (e.g., for
frameworks Spark or TensorFlow, or benchmark like IOR or mdtest that support
different a storage backend plugin), or transparently via a FUSE daemon, combined
optionally with an interception library to address some of the FUSE performance
bottleneck by delivering full OS bypass for POSIX read/write operations.

### libdfs

DFS stands for DAOS File System and is a library that allows a DAOS container to
be accessed as a hierarchical POSIX namespace. It supports files, directories,
and symbolic links, but not hard links. Access permissions are inherited from
the parent pool and not implemented on a per-file or per-directory basis.
setuid() and setgid() programs, as well as supplementary groups, are currently not
supported.

While libdfs can be tested from a single instance (i.e. single process or client
node if used through dfuse), special care is required when the same POSIX
container is mounted concurrently by multiple processes. Concurrent DFS mounts
are not recommended. Support for concurrency control is under development and
will be documented here once ready.

### dfuse

A fuse daemon called dfuse is provided to mount a POSIX container in the local
filesystem tree. dfuse exposes one mountpoint as a single DFS namespace with a
single pool and container and can be mounted by regular use (provided that it
is granted access to the pool and container).
To mount an existing POSIX container with dfuse, run the following command:

```
$ dfuse --pool a171434a-05a5-4671-8fe2-615aa0d05094 -s 0 --container 464e68ca-0a30-4a5f-8829-238e890899d2 -m /tmp/daos
```

The UUID after -p and -c should be replaced with respectively the pool and
container UUID. -s should be followed by the pool svc rank list and -m is the
local directory where the mount point will be setup.
When done, the file system can be unmounted via fusermount:

```
$ fusermount3 -u /tmp/daos
```

### libioil

An interception library called libioil is available to work with dfuse. This
library works in conjunction with dfuse and allow to interception of POSIX I/O
calls and issue the I/O operations directly from the application context through
libdaos without any appliction changes.  This provides kernel-bypass for I/O data
leading to improved performance.
To use this set the LD_PRELOAD to point to the shared libray in the DOAS install
dir

LD_PRELOAD=/path/to/daos/install/lib/libioil.so

Support for libioil is currently planned for DAOS v1.2.

## Unified Namespace

The DAOS tier can be tightly integrated with the Lustre parallel filesystem in
which DAOS containers will be represented through the Lustre namespace. This
capability is under development and is scheduled for DAOS v1.2.

Current state of work can be summarized as follow :

-   DAOS integration with Lustre uses the Lustre foreign file/dir feature
    (from LU-11376 and associated patches)

-   each time a DAOS POSIX container is created, using `daos` utility and its
    '--path' UNS option, a Lustre foreign file/dir of 'daos' type is being
    created with a specific LOV/LMV EA content that will allow to store the
    DAOS pool and containers UUIDs.

-   Lustre Client patch for LU-12682, adds DAOS specific support to the Lustre
    foreign file/dir feature. It allows for foreign file/dir of `daos` type
    to be presented and act as `<absolute-prefix>/<pool-uuid>/<container-uuid>`
    a symlink to the Linux Kernel/VFS.

-   the <absolute-prefix> can be specified as the new `daos=<absolute-prefix>`
    Lustre Client mount option, or also thru the new `llite.*.daos_prefix`
    Lustre dynamic tuneable. And both <pool-uuid> and <container-uuid> are
    extracted from foreign file/dir LOV/LMV EA.

-   to allow for symlink resolution and transparent access to DAOS concerned
    container content, it is expected that a DFuse/DFS instance/mount, of
    DAOS Server root, exists on <absolute-prefix> presenting all served
    pools/containers as `<pool-uuid>/<container-uuid>` relative paths.

-   `daos` foreign support is enabled at mount time with `daos=` option
    present, or dynamically thru `llite.*.daos_enable` setting.

## HPC I/O Middleware Support

Several HPC I/O middleware libraries have been ported to the native API.

### MPI-IO

DAOS has its own MPI-IO ROM ADIO driver located in a MPICH fork on GitHub:

<https://github.com/daos-stack/mpich>

This driver has been submitted upstream for integration.

To build the MPI-IO driver:

-   export MPI_LIB=""

-   download the mpich repo from above and switch to daos_adio branch

-   ./autogen.sh

-   mkdir build; cd build

-   ../configure --prefix=dir --enable-fortran=all --enable-romio
    --enable-cxx --enable-g=all --enable-debuginfo
    --with-file-system=ufs+daos --with-daos=dir --with-cart=dir

-   make -j8; make install

Switch the PATH and LD_LIBRARY_PATH to where you want to build your client apps or libs
that use MPI to the installed MPICH.

Build any client (HDF5, ior, mpi test suites) normally with the mpicc and mpich
library installed above (see child pages).

To run an example:

1. Launch DAOS server(s) and create a pool as specified in the previous section.
   This will return a pool uuid "puuid" and service rank list "svcl"
2.   At the client side, the following environment variables need to be set:

        export PATH=/path/to/mpich/install/bin:$PATH
        export LD_LIBRARY_PATH=/path/to/mpich/install/lib:$LD_LIBRARY_PATH
        export MPI_LIB=""
2.  export DAOS_POOL=puuid; export DAOS_SVCL=svcl
    This is just temporary till we have a better way of passing pool
    connect info to MPI-IO and other middleware over DAOS.
3.  Run the client application or test.

Limitations to the current implementation include:

-   Incorrect MPI_File_set_size and MPI_File_get_size - This will be fixed in
    the future when DAOS correctly supports records enumeration after punch or
    key query for max/min key and recx.

-   Reading Holes does not return 0, but leaves the buffer untouched

-   No support for MPI file atomicity, preallocate, shared file pointers.

### HDF5

A prototype version of an HDF5 DAOS connector is available. Please refer to the
DAOS VOL connector user guide[^3] for instructions on how to build and use it.

## Spark Support

Spark integration with libdfs is under development and is scheduled for DAOS
v1.0 or v1.2.

## Data Migration

### Migration to/from a POSIX filesystem

A dataset mover tool is under consideration to move a snapshot of a POSIX,
MPI-IO or HDF5 container to a POSIX filesystem and vice versa. The copy will be
performed at the POSIX or HDF5 level. The resulting HDF5 file over the POSIX
filesystem will be accessible through the native HDF5 connector with the POSIX
VFD.

The first version of the mover tool is currently scheduled for DAOS v1.4.

### Container Parking

The mover tool will also eventually support the ability to serialize and
deserialize a DAOS container to a set of POSIX files that can be stored or
"parked" in an external POSIX filesystem. This transformation is agnostic to the
data model and container type and will retain all DAOS internal metadata.

[^1]: https://github.com/daos-stack/daos/blob/master/src/client/pydaos/raw/README.md

[^2]: https://godoc.org/github.com/daos-stack/go-daos/pkg/daos

[^3]: https://bitbucket.hdfgroup.org/projects/HDF5VOL/repos/daos-vol/browse/README.md
