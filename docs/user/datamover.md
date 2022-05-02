# Data Mover

The Dataset Mover is a collection of multiple tools that allow users to copy
and serialize data across DAOS and POSIX file systems. There is support for
data movement across POSIX, DAOS, and HDF5 containers.
There is also support for serializing and deserializing a DAOS container,
where a representation of the container is stored on a POSIX filesystem
in an HDF5 file(s) and can be restored to a new DAOS container.

## Overview of Tools

These tools are implemented within the `daos` command.

- `daos filesystem copy` - Copy between POSIX containers and POSIX filesystems using the `libdfs` library.
- `daos container clone` - Copy any container to a new container using the   Object API (`libdaos` library).

These tools have MPI support and are implemented in the external
[MpiFileutils](https://github.com/hpc/mpifileutils) repository.

- `dcp` - Copy between POSIX containers and POSIX filesystems using the `libdfs` library, or copy between any two DAOS containers using the Object API (`libdaos` library).
- `dsync` - Similar to `dcp`, but attempts to only copy the difference   between the source and destination.
- `daos-serialize` - Serialize any DAOS container to an HDF5 file(s).
- `daos-deserialize` - Deserialize any DAOS container that was serialized with `daos-serialize`.

More documentation and uses cases for these tools can be found
[here](https://github.com/hpc/mpifileutils/blob/release/2.2/DAOS-Support.md).

Build instructions for these tools can be found
[here](https://mpifileutils.readthedocs.io/en/latest/build.html#build-everything-directly-with-daos-support).

## DAOS Tools Usage

### `daos filesystem copy`

There are two mandatory command-line options; these are:

| **Command-line Option**               | **Description**      |
| ------------------------------------- | -------------------- |
| --src=daos://<pool/cont\> \| <path\>  | the source path      |
| --dst=daos://<pool/cont\> \| <path\>  | the destination path |

!!! note
    In DAOS 1.2, only directories are supported as the source or destination.
    Files, directories, and symbolic links are copied from the source directory.

#### Example #1

Copy a POSIX container to a POSIX filesystem:

```shell
daos filesystem copy --src daos://<pool_uuid>/<cont_uuid> --dst <posix_path>
```

Copy from a POSIX filesystem to a sub-directory in a POSIX container:

```shell
 daos filesystem copy --src <posix_path> --dst daos://<pool_uuid>/<cont_uuid>/<sub_dir>
```

Copy from a POSIX container by specifying a UNS path:

```shell
 daos filesystem copy --src <uns_path> --dst <posix_path>
```

### `daos container clone`

There are two mandatory command-line options; these are:

| **Command-line Option**                   | **Description**           |
| ----------------------------------------- | ------------------------- |
| --src=daos://<pool/cont\> \| <path\>      | the source container      |
| --dst=daos://<pool\>[/cont\>] \| <path\>  | the destination container |

The destination container must not already exist.

#### Examples #2

Clone a container to a new container with a given UUID:

```shell
daos container clone --src /<pool_uuid>/<cont_uuid> --dst /<pool_uuid>/<new_cont_uuid>
```

Clone a container to a new container with an auto-generated UUID:

```shell
daos container clone --src /<pool_uuid>/<cont_uuid> --dst /<pool_uuid>
```
