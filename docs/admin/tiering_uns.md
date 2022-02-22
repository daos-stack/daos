# Tiering and Unified Namespace

## Unified Namespace

The DAOS tier can be tightly integrated with the Lustre parallel filesystem,
in which DAOS containers will be represented through the Lustre namespace.


The current state of this work can be summarized as follow :

-   DAOS integration with Lustre uses the Lustre foreign file/dir feature
    (from LU-11376 and associated patches).

-   Each time a DAOS POSIX container is created, using the `daos` utility and its
    '--path' UNS option, a Lustre foreign file/dir of 'daos' type is being
    created with a specific LOV/LMV EA content that will allow to store the
    DAOS pool and containers UUIDs.

-   Lustre Client patch for LU-12682, adds DAOS specific support to the Lustre
    foreign file/dir feature. It allows for foreign file/dir of `daos` type
    to be presented and act as an `<absolute-prefix>/<pool-uuid>/<container-uuid>`
    symlink to the Linux Kernel/VFS.

-   The <absolute-prefix> can be specified as the new `daos=<absolute-prefix>`
    Lustre Client mount option, or also through the new `llite.*.daos_prefix`
    Lustre dynamic tuneable. Both <pool-uuid> and <container-uuid> are
    extracted from foreign file/dir LOV/LMV EA.

-   To allow for symlink resolution and transparent access to DAOS
    container content, it is expected that a DFuse/DFS instance/mount, of
    DAOS Server root, exists on <absolute-prefix> presenting all served
    pools/containers as `<pool-uuid>/<container-uuid>` relative paths.

-   `daos` foreign support is enabled at mount time with the `daos=` option
    present, or dynamically through `llite.*.daos_enable` setting.

## Data Migration

### Migration to/from a POSIX filesystem

A dataset mover tool is under development to move a snapshot of a DAOS POSIX
container or DAOS HDF5 container to a POSIX filesystem and vice versa.
The copy will be performed at the POSIX or HDF5 level.
(The MPI-IO ROMIO ADIO driver for DAOS also uses DAOS POSIX containers.)
For DAOS HDF5 containers, the resulting HDF5 file in the POSIX filesystem
will be accessible through the native HDF5 connector with the POSIX VFD.

The first version of the data mover tool is currently scheduled for DAOS v1.4.

### Container Parking

The mover tool will also eventually support the ability to serialize and
deserialize a DAOS container to a set of POSIX files that can be stored or
"parked" in an external POSIX filesystem. This transformation is agnostic to the
data model and container type, and will retain all DAOS internal metadata.
