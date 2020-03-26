# POSIX Namespace

A regular POSIX namespace can be encapsulated into a DAOS container.  This
capability is provided by the libdfs library that implements the file and
directory abstractions over the native libdaos library. The POSIX emulation can
be exposed to applications or I/O frameworks either directly (e.g., for
frameworks Spark or TensorFlow, or benchmark like IOR or mdtest that support
different a storage backend plugin), or transparently via a FUSE daemon, combined
optionally with an interception library to address some of the FUSE performance
bottleneck by delivering full OS bypass for POSIX read/write operations.

![../graph/posix.png](../graph/posix.png "POSIX I/O Support")

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

```bash
$ dfuse --pool a171434a-05a5-4671-8fe2-615aa0d05094 -s 0 --container 464e68ca-0a30-4a5f-8829-238e890899d2 -m /tmp/daos
```

The UUID after -p and -c should be replaced with respectively the pool and
container UUID. -s should be followed by the pool svc rank list and -m is the
local directory where the mount point will be setup.
When done, the file system can be unmounted via fusermount:

```bash
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
