# Tiering and Unified Namespace

## Unified Namespace

The DAOS tier can be tightly integrated with the Lustre parallel filesystem,
in which DAOS containers will be represented through the Lustre namespace.
This capability is under development and is scheduled for DAOS v1.2.

The current state of work can be summarized as follow :

-   DAOS integration with Lustre uses the Lustre foreign file/dir feature
    (from LU-11376 and associated patches).

-   Each time a DAOS POSIX container is created, using the `daos` utility and its
    '--path' UNS option, a Lustre foreign file/dir of 'symlink' type is being
    created with a specific LOV/LMV EA content that will allow to store the
    DAOS pool and containers UUIDs.

-   Lustre Client patch for LU-12682, adds DAOS specific support to the Lustre
    foreign file/dir feature. It allows for foreign file/dir of `symlink` type
    to be presented and act as an `<absolute-prefix>/<pool-uuid>/<container-uuid>`
    symlink to the Linux Kernel/VFS.

-   The <absolute-prefix> can be specified as the new `foreign_symlink=<absolute-prefix>`
    Lustre Client mount option, or also through the new `llite.*.foreign_symlink_prefix`
    Lustre dynamic tuneable. Both <pool-uuid> and <container-uuid> are
    extracted from foreign file/dir LOV/LMV EA.

-   To allow for symlink resolution and transparent access to DAOS
    container content, it is expected that a DFuse/DFS instance/mount, of
    DAOS Server root, exists on <absolute-prefix> presenting all served
    pools/containers as `<pool-uuid>/<container-uuid>` relative paths.

-   `daos` foreign support is enabled at mount time with the `symlink=` option
    present, or dynamically through `llite.*.daos_enable` setting.

### Building and using a DAOS-aware Lustre version

-   as indicated before, a Lustre Client patch (for LU-12682) has been developed
    to allow for application's transparent access to DAOS container's data
    from a Lustre foreign file/dir.

-   this patch can be found at https://review.whamcloud.com/35856 and it has
    already landed onto master but is still not integrated in any official
    Lustre version so it may have to be applied on top of the selected Lustre
    version's source tree.

-   after any conflict will have been fixed, Lustre will have to be built and
    generated RPMs installed on client nodes, by following instructions at
    https://wiki.whamcloud.com/display/PUB/Building+Lustre+from+Source .

-   Lustre client mount command will have to use the new
    `foreign_symlink=<absolute_path>` option to set the prefix to be used in
    front of the <pool-UUID>/<cont-UUID> relative path based on pool/container
    information being extracted from LOV/LMV foreign symlink EAs. This can also
    be configured by dynamically modifying both `foreign_symlink_[enable,prefix]`
    parameters for each/any Lustre client mount, using the
    `lctl set_param llite/*/foreign_symlink_[enable,prefix]=[0|1,<path>]` command.
    Dfuse instance will then need to use this prefix to mount/expose all
    DAOS pools, or use <prefix>/<pool-UUID>[/<cont-UUID>] to mount a
    single pool[/container].

-   to allow non-root/admin users to be able to use the llapi_set_dirstripe()
    API (like the `daos cont create` command with `--path` option) or the
    `lfs setdirstripe` command, Lustre MDS servers configuration will need to
    be modified accordingly by running the
    `lctl set_param mdt/*/enable_remote_dir_gid=-1` command.

-   in addition, there is a feature available to provide a customized format
    of LOV/LMV EAs, different from default `<pool-UUID>/<cont-UUID>`, throug the
    `llite/*/foreign_symlink_upcall` tunable. It allows to provide the path
    of a user-land upcall which will have to indicate  where to extract both
    `<pool-UUID>` and `<cont-UUID>` in LOV/LMV EAs, using a series of [pos, len]
    tuples and constant strings. `lustre/utils/l_foreign_symlink.c` is a helper
    example in Lustre source code.

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
