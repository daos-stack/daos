# Tiering and Unified Namespace

## Unified Namespace

The DAOS tier can be tightly integrated with the Lustre parallel filesystem,
in which DAOS containers will be represented through the Lustre namespace.

The current state of work can be summarized as follows :

-   DAOS integration with Lustre uses the Lustre foreign file/dir feature
    (from LU-11376 and associated patches).

-   Each time a DAOS POSIX container is created using the `daos` utility and its
    '--path' UNS option, a Lustre foreign file/dir of 'symlink' type is
    created with a specific LOV/LMV EA content that will allow the
    DAOS pool and containers UUIDs to be stored.

-   The Lustre Client patch for LU-12682 adds DAOS specific support to the Lustre
    foreign file/dir feature. It allows for the foreign file/dir of `symlink` type
    to be presented and act as an `<absolute-prefix>/<pool-uuid>/<container-uuid>`
    symlink to the Linux Kernel/VFS.

-   The `<absolute-prefix>` can be specified as the new `foreign_symlink=<absolute-prefix>`
    Lustre Client mount option, or also through the new `llite.*.foreign_symlink_prefix`
    Lustre dynamic tuneable. Both `<pool-uuid>` and `<container-uuid>` are
    extracted from foreign file/dir LOV/LMV EA.

-   To allow for symlink resolution and transparent access to the DAOS
    container content, it is expected that a DFuse/DFS instance/mount of
    DAOS Server root exists on `<absolute-prefix>`, presenting all served
    pools/containers as `<pool-uuid>/<container-uuid>` relative paths.

-   `daos` foreign support is enabled at mount time with the `symlink=` option
    present or dynamically, through the `llite.*.daos_enable` setting.

### Building and using a DAOS-aware Lustre version

As indicated before, a Lustre Client patch (for LU-12682) has been developed
    to allow for the application's transparent access to the DAOS container's data
    from a Lustre foreign file/dir.

This patch can be found at https://review.whamcloud.com/35856 and has
    been landed onto master but is still not integrated with an official
    Lustre version. This patch must be applied on top of the selected Lustre
    version's source tree.

After any conflicts are resolved, Lustre must be built and
    the generated RPMs installed on client nodes by following the instructions at
    https://wiki.whamcloud.com/display/PUB/Building+Lustre+from+Source.

The Lustre client mount command must use the new
    `foreign_symlink=<absolute_path>` option to set the prefix to be used in
    front of the `<pool-UUID>/<cont-UUID>` relative path, based on pool/container
    information being extracted from the LOV/LMV foreign symlink EAs. This can
    be configured by dynamically modifying both `foreign_symlink_[enable,prefix]`
    parameters for each Lustre client mount, using the
    `lctl set_param llite/*/foreign_symlink_[enable,prefix]=[0|1,<path>]` command.
    The Dfuse instance will then use this prefix to mount/expose all
    DAOS pools, or use `<prefix>/<pool-UUID>[/<cont-UUID>]` to mount a
    single pool/container.

To allow non-root/admin users to use the llapi_set_dirstripe()
    API (like the `daos cont create` command with `--path` option), or the
    `lfs setdirstripe` command, the Lustre MDS servers configuration must
    be modified accordingly by running the
    `lctl set_param mdt/*/enable_remote_dir_gid=-1` command.

 Additionally, there is a feature available to provide a customized format
    of LOV/LMV EAs, apart from the default `<pool-UUID>/<cont-UUID>`, through the
    `llite/*/foreign_symlink_upcall` tunable. This provides the path
    of a user-land upcall, that will indicate  where to extract
    `<pool-UUID>` and `<cont-UUID>` in the LOV/LMV EAs, using a series of [pos, len]
    tuples and constant strings. `lustre/utils/l_foreign_symlink.c` is a helper
    example in the Lustre source code.

## Data Migration

### Migration to/from a POSIX filesystem

A dataset mover tool is under development to move a snapshot of a DAOS POSIX
container or DAOS HDF5 container to a POSIX filesystem and vice versa.
The copy will be performed at the POSIX or HDF5 level.
(The MPI-IO ROMIO ADIO driver for DAOS also uses DAOS POSIX containers.)
For DAOS HDF5 containers, the resulting HDF5 file in the POSIX filesystem
will be accessible through the native HDF5 connector with the POSIX VFD.

The POSIX data mover was released with DAOS v1.2 and supports data migration
to/from a POSIX filesystem. Parallel data migration is available through
mpiFileUtils, which contains a DAOS backend. Serial data migration is supported
through the daos filesystem copy utility.
A version of the data mover tool that contains support for HDF5 containers
is planned for a future release of DAOS.

### Container Parking

The mover tool supports the ability to serialize and deserialize a DAOS
container to a set of POSIX files that can be stored or “parked” in an external
POSIX filesystem. This transformation is agnostic to the data model and
container type and retains most DAOS internal metadata. The serialized file(s)
are written to a POSIX filesystem in an HDF5 file format. A preview of the
serialization and deserialization tools is available through
mpiFileUtils, and they will be officially released in a future DAOS release.

More details and instructions on data mover usage can be found at:
https://github.com/daos-stack/daos/blob/master/docs/user/datamover.md
