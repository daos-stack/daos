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

## libdfs

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

## dfuse

DFuse provides File System access to DAOS through the standard libc/kernel/VFS
POSIX infrastructure.  This allows existing applications to use DAOS without
modification, and provides a path to upgrade those applications to native DAOS
support.  In addition DFuse provides an Interception Library to transparrently
allow POSIX clients to talk directly to DAOS servers providing OS-Bypass for
I/O without modifying of recompiling of the application.

DFuse builds heavily on DFS and data written via DFuse can be access by DFS and
vice versa.

### DFuse Deamon

The dfuse daemon runs a single instance per node to provide a user POSIX access
to DAOS, it should be run with the credentials of the user and typically will
be started and stopped on each compute node as part of the prolog and epilog
scripts of any resource manager or scheduler in use.  One dfuse daemon per node
can process requests for multiple clients.

A single dfuse instance can provide access to multiple pools and containers
concurrently, or can be limited to a single pool, or a single container.

### Restrictions

dfuse is limited to a single user, access to the filesystem from other users,
including root will not be honoured and as a consequence of this the chown
and chgrp calls are not supported.  Hard links, and special device files with
the exception of symbolic links are not supported, nor are any ACLs.

dfuse can run in the foreground, keeping the terminal window open, or it can
deamonise to run like a system daemon, however in order to do this and still be
able to access DAOS it needs to deamonise before calling daos_init() which in
turns means it cannot report some kinds of startup errors either on
stdout/stderr or via it's return code.  When initially starting with dfuse it
is recommended to run in foreground mode (--foreground) to better observe
any failures.

Inodes are managed on the local node by the dfuse, so whilst inode numbers
will be consistent on a node for the duration of the session they are not
guaranteed to be consistent across restarts of dfuse or across nodes.

It is not possible to see pool/container listings through dfuse so if readdir
or ls etc. are used for this dfuse will return ENOTSUP.

### Launching

dfuse should be run with the credentails (user/group) of the user that will
be accessing it, and that owns any pools that will be used.

There are two mandatory command line options, these are

* --svc=RANKS  <service replicas>
* --mountpount=PATH <path to mount DAOS>

In addition, there are several optional command line options

* --pool=POOL <pool uuid to connect to>
* --container=CONTAINER <container uuid to open>
* --sys-name=NAME <DAOS server name>
* --foreground <run in foreground>
* --singlethreaded <run single threaded>

When dfuse starts it will register a single mount with the kernel at the
location specified by the --mountpoint option, and this mount will be
visable in /proc/mounts and possibly the output of df.  The contents of
multiple pools/containers will be accessible via this single kernel
mountpoint.

### Pool/Container paths.

dfuse will only create one kernel level mount point regardless of how it is
launched, but how POSIX containers are represented within that varies depending
on the options.

If both a pool and container uuids are specified on the command line then the
mount point will map to the root of the container itself so files can be
accessed by simply concatinating the mount point and the name of the file,
relative to the root of the container.

If neither a pool or container is specified then pools and container can be
accessed by the path `<mount point>/<pool uuid>/<container uuid>` however it
should be noted that readdir() and therefore ls do not work on either mount
points or directories representing pools here so the pool and container uuids
will have to be provided from an external source.

If a pool uuid is specified but not a container uuid the containers can be
accessed by the path `<mount point>/<container uuid`

It is anticipated that in most cases both pool and container uuids shall be
used, so the mount point itself will map directly onto a POSIX container.

### Stopping dfuse.

When done, the file system can be unmounted via fusermount:

```bash
$ fusermount3 -u /tmp/daos
```

When this is done the local dfuse deamon should shut down the mount point,
disconnect from the DAOS servers and exit.  You can also verify that the
mount point is no longer listed in the /proc/mounts file

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
