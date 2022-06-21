# File System

A container can be mounted as shared POSIX namespace on multiple compute nodes.
This capability is provided by the `libdfs` library that implements the file and
directory abstractions over the native `libdaos` library. The POSIX emulation can
be exposed directly to applications or I/O frameworks (e.g., for frameworks like
Spark or TensorFlow, or benchmarks like IOR or mdtest that support different
storage backend plugins).
It can also be exposed transparently via a FUSE daemon, combined optionally with
an interception library to address some of the FUSE performance bottlenecks by
delivering full OS bypass for POSIX read/write operations.

![../graph/posix.png](../graph/posix.png "POSIX I/O Support")

The performance is going to be best generally when using the DFS API directly.
Using the IO interception library with dfuse should yield the same performance
for IO operations (read/write) as the DFS API with minimal overhead. Performance
of metadata operations (file creation, deletion, rename, etc.) over dfuse will
be much slower than the DFS API since there is no interception to bypass the
fuse/kernel layer.

## libdfs

The DAOS File System (DFS) is implemented in the `libdfs` library,
and allows a DAOS container to be accessed as a hierarchical POSIX namespace.
`libdfs` supports files, directories, and symbolic links, but not hard links.
Access permissions are inherited from the parent pool and are not implemented on
a per-file or per-directory basis.

### Supported Operations

The DFS API closely represents the POSIX API. The API includes operations to:

* Mount: create/open superblock and root object
* Un-mount: release open handles
* Lookup: traverse a path and return an open file/dir handle
* IO: read & write with an iovec
* Stat: retrieve attributes of an entry
* Mkdir: create a dir
* Readdir: enumerate all entries under a directory
* Open: create/Open a file/dir
* Remove: unlink a file/dir
* Move: rename
* Release: close an open handle of a file/dir
* Extended Attributes: set, get, list, remove

### POSIX Compliance

The following features from POSIX are not supported:

* Hard links
* mmap support with MAP\_SHARED will be consistent from single client only. Note
  that this is supported through DFUSE only (i.e. not through the DFS API).
* Char devices, block devices, sockets and pipes
* User/group quotas
* setuid(), setgid() programs, supplementary groups, POSIX ACLs are not supported
  within the DFS namespace.
* [access/change/modify] time not updated appropriately, potentially on close only.
* Flock (maybe at dfuse local node level only)
* Block size in stat buf is not accurate (no account for holes, extended attributes)
* Various parameters reported via statfs like number of blocks, files,
  free/available space
* POSIX permissions inside an encapsulated namespace
  * Still enforced at the DAOS pool/container level
  * Effectively means that all files belong to the same "project"

!!! note
    DFS directories do not include the `.` (current directory) and `..` (parent directory)
    directory entries that are known from other POSIX filesystems.
    Commands like `ls -al` will not include these entries in their output.
    Those directory entries are not required by POSIX, so this is not a limitation to POSIX
    compliance. But scripts that parse directory listings under the assumption that those dot
    directories are present may need to be adapted to to correctly handle this situation.
    Note that operations like `cd .` or `cd ..` will still succeed in dfuse-mounted POSIX
    containers.

It is possible to use `libdfs` in a parallel application from multiple nodes.
DFS provides two modes that offer different levels of consistency. The modes can
be set on container creation time:

1) Relaxed mode for well-behaved applications that generate conflict-free
operations for which a very high level of concurrency will be supported.

2) Balanced mode for applications that require stricter consistency at the cost
of performance. This mode is currently not fully supported and DFS by default
will use the relaxed mode.

On container access, if the container is created with balanced mode, it can be
accessed in balanced mode only. If the container was created with relaxed mode,
it can be accessed in relaxed or balanced mode. In either mode, there is a
consistency semantic issue that is not properly handled:

* Open-unlink semantics: This occurs when a client obtains an open handle on an
  object (file or directory), and accesses that object (reads/writes data or
  create other files), while another client removes that object that the other
  client has opened from under it. In DAOS, we don't track object open handles
  as that would be very expensive, and so in such conflicting cases, the worst
  case scenario is the lost/leaked space that is written to those orphan objects
  that have been unlinked from the namespace.

Other consistency issues are handled differently between the two consistency mode:

* Same Operation Executed Concurrently (Supported in both Relaxed and Balanced
  Mode): For example, clients try to create or remove the same file
  concurrently, one should succeed and others will fail.
* Create/Unlink/Rename Conflicts (Supported in Balanced Mode only): For example,
  a client renames a file, but another unlinks the old file at the same time.
* Operation Atomicity (Supported only in Balanced mode): If a client crashes in
  the middle of the rename, the state of the container should be consistent as
  if the operation never happened.
* Visibility (Supported in Balanced and Relaxed mode): A write from one client
  should be visible to another client with a simple coordination between the
  clients.

## DFuse (DAOS FUSE)

DFuse provides DAOS File System access through the standard libc/kernel/VFS
POSIX infrastructure.  This allows existing applications to use DAOS without
modification, and provides a path to upgrade those applications to native DAOS
support.  Additionally, DFuse provides an Interception Library `libioil` to
transparently allow POSIX clients to talk directly to DAOS servers, providing
OS-Bypass for I/O without modifying or recompiling of the application.

DFuse builds heavily on DFS. Data written via DFuse can be accessed by DFS and
vice versa.

### DFuse Daemon

The `dfuse` daemon runs a single instance per node to provide a user POSIX access
to DAOS. It should be run with the credentials of the user, and typically will
be started and stopped on each compute node as part of the prolog and epilog
scripts of any resource manager or scheduler in use.

### Core binding and threads.

DFuse will launch one thread per available core by default, although this can be
changed by the `--thread-count` option. To change the cores that DFuse runs on
use kernel level tasksets which will bind DFuse to a subset of cores. This can be
done via the `tasket` or `numactl` programs or similar. If doing this then DFuse
will again launch one thread per available core by default.  Many metadata
operations will block a thread until completed so if restricting DFuse to a small
number of cores then overcommiting via the `--thread-count` option is desirable.

### Restrictions

DFuse is limited to a single user. Access to the filesystem from other users,
including root, will not be honored. As a consequence of this, the `chown`
and `chgrp` calls are not supported.  Hard links and special device files,
except symbolic links, are not supported, nor are any ACLs.

DFuse can run in the foreground, keeping the terminal window open, or it can
daemonize to run like a system daemon.
However, to do this and still be able to access DAOS it needs to daemonize
before calling `daos_init()`. This in turns means it cannot report some kinds
of startup errors either on stdout/stderr or via its return code.
When initially starting with DFuse it is recommended to run in foreground mode
(`--foreground`) to better observe any failures.

Inodes are managed on the local node by DFuse. So while inode numbers
will be consistent on a node for the duration of the session, they are not
guaranteed to be consistent across restarts of DFuse or across nodes.

It is not possible to see pool/container listings through DFuse.
So if `readdir`, `ls` or others are used, DFuse will return `ENOTSUP`.

### Launching

DFuse should be run with the credentials (user/group) of the user who will
be accessing it, and who owns any pools that will be used.

There are two mandatory command-line options, these are:

| **Command-line Option**  | **Description**     |
| ------------------------ | ------------------- |
| --mountpoint=<path\>     | path to mount dfuse |

The mount point specified should be an empty directory on the local node that
is owned by the user.

Additionally, there are several optional command-line options:

| **Command-line Option**    | **Description**                  |
| -------------------------- | -------------------------------- |
| --pool=<label\|uuid\>      | pool label or uuid to connect to |
| --container=<label\|uuid\> | container label or uuid to open  |
| --sys-name=<name\>         | DAOS system name                 |
| --foreground               | run in foreground                |
| --singlethreaded           | run single threaded              |
| --thread-count=<count>     | Number of threads to use         |

When DFuse starts, it will register a single mount with the kernel, at the
location specified by the `--mountpoint` option. This mount will be
visible in `/proc/mounts`, and possibly in the output of `df`.
The contents of multiple pools/containers will be accessible via this
single kernel mountpoint.

Below is an example of creating and mounting a POSIX container under
the /tmp/dfuse mountpoint.

```bash
$ mkdir /tmp/dfuse

$ dfuse -m /tmp/dfuse --pool tank --cont mycont

$ touch /tmp/dfuse/foo

$ ls -l /tmp/dfuse/
total 0
-rw-rw-r-- 1 jlombard jlombard 0 Jul 10 20:23 foo

$ df -h /tmp/dfuse/
Filesystem      Size  Used Avail Use% Mounted on
dfuse           9.4G  326K  9.4G   1% /tmp/dfuse
```

### Links into other Containers

It is possible to link to other containers in DFuse, where subdirectories
within a container resolve not to regular directories, but rather to
the root of entirely different POSIX containers.

To create a new container and link it into the namespace of an existing one,
use the following command.

```bash
$ daos container create <pool_label> --type POSIX --path <path_to_entry_point>
```

The pool should already exist, and the path should specify a location
somewhere within a DFuse mount point that resolves to a POSIX container.
Once a link is created, it can be accessed through the new path. Following
the link is virtually transparent.  No container uuid is required. If one is
not supplied, it will be created.

To destroy a container again, the following command should be used.

```bash
$ daos container destroy --path <path to entry point>
```

This will both remove the link between the containers and remove the container
that was linked to.

There is no support for adding links to already existing containers or removing
links to containers without also removing the container itself.

Information about a container, for example, the presence of an entry point between
containers, or the pool and container uuids of the container linked to can be
read with the following command.
```bash
$ daos container info --path <path to entry point>
```

Please find below an example.

```bash
$ dfuse -m /tmp/dfuse --pool tank --cont mycont
$ cd /tmp/dfuse/
$ ls
foo
$ daos cont create tank --label mycont3 --type POSIX --path ./link_to_externa_container
  Container UUID : 933944a9-ddf2-491a-bdbf-4442f0437d56
  Container Label: mycont3
  Container Type : POSIX

Successfully created container 933944a9-ddf2-491a-bdbf-4442f0437d56 type POSIX
$ ls -lrt
total 0
-rw-rw-r-- 1 jlombard jlombard  0 Jul 10 20:23 foo
drwxr-xr-x 1 jlombard jlombard 72 Jul 10 20:56 link_to_externa_container
$ daos cont destroy --path ./link_to_externa_container/
Successfully destroyed container 933944a9-ddf2-491a-bdbf-4442f0437d56
jlombard@wolf-151:/tmp/dfuse$ ls -l
total 0
-rw-rw-r-- 1 jlombard jlombard 0 Jul 10 20:23 foo
```

### Caching

For performance reasons caching will be enabled by default in DFuse, including both
data and metadata caching.  It is possible to tune these settings both at a high level
on the DFuse command line and fine grained control via container attributes.

The following types of data will be cached by default.

* Kernel caching of dentries
* Kernel caching of negative dentries
* Kernel caching of inodes (file sizes, permissions etc)
* Kernel caching of file contents
* Kernel caching of directory contents (when supported by libfuse)
* MMAP write optimization

!!! warning
    Caching is enabled by default in dfuse. This might cause some parallel
    applications to fail. Please disable caching (--disable-caching option) if
    you experience this or want up to date data sharing between nodes.

To selectively control caching within a container the following container
attributes should be used, if any attribute is set then the rest are assumed
to be set to 0 or off, except dentry-dir-time which defaults to dentry-time

| **Attribute name**      | **Description**                                                        |
| ----------------------- | ---------------------------------------------------------------------- |
| dfuse-attr-time         | How long file attributes are cached                                    |
| dfuse-dentry-time       | How long directory entries are cached                                  |
| dfuse-dentry-dir-time   | How long dentries are cached, if the entry is itself a directory       |
| dfuse-ndentry-time      | How long negative dentries are cached                                  |
| dfuse-data-cache        | Data caching enabled for this file ("on"/"true"/"off"/"false")         |
| dfuse-direct-io-disable | Force use of page cache for this container ("on"/"true"/"off"/"false") |

For metadata caching attributes specify the duration that the cache should be
valid for, specified in seconds or with a 's', 'm', 'h' or 'd' suffix for seconds,
minutes, hours or days.

dfuse-data-cache should be set to "on", "true", "off" or "false" if set, other values will
log an error, and result in the cache being off.  The O\_DIRECT flag for open
files will be honoured with this option enabled, files which do not set
O\_DIRECT will be cached.

dfuse-direct-io-disable will enable data caching, similar to dfuse-data-cache,
however if this is enabled then the O\_DIRECT flag will be ignored, and all
files will use the page cache.  This default value for this is disabled.

With no options specified attr and dentry timeouts will be 1 second, dentry-dir
and ndentry timeouts will be 5 seconds, and data caching will be enabled.

Readir caching will be enabled when the dfuse-dentry-time setting is non-zero and when supported by
libfuse; however, on many distributions the system libfuse is not able to support this feature.
Libfuse version 3.5.0 or newer is required at both compile and run-time.  Use `dfuse --version` or
the runtime logs to see the fuse version used and if the feature is compiled into dfuse.

These are two command line options to control the DFuse process itself.

| **Command line option** | **Description**           |
| ----------------------- | ------------------------- |
| --disable-caching       | Disables all caching      |
| --disable-wb-caching    | Disables write-back cache |

These will affect all containers accessed via DFuse, regardless of any
container attributes.

### Permissions

DFuse can serve data from any user's container, but needs appropriate permissions in order to do
this.

File ownership within containers is set by the container being served, with the owner of the
container owning all files within that container, so if looking at the container of another user
then all entries within that container will be owned by that user, and file-based permissions
checks by the kernel will be made on that basis.

Should write permission be granted to another user then any newly created files will also be
owned by the container owner, regardless of the user used to create them.  Permissions are only
checked on connect, so if permissions are revoked users need to
restart DFuse for these to be picked up.

#### Pool permissions.

DFuse needs 'r' permission for pools only.

#### Container permissions.

DFuse needs 'r' and 't' permissions to run: read for accessing the data, 't' to read container
properties to know the container type. For older layout versions (containers created by DAOS v2.0.x
and before), 'a' permission is also required to read the ACLs to know the container owner.

Write permission for the container is optional; however, without it the container will be read-only.

### Stopping DFuse

When done, the file system can be unmounted via fusermount:

```bash
$ fusermount3 -u /tmp/daos
```

When this is done, the local DFuse daemon should shut down the mount point,
disconnect from the DAOS servers, and exit.  You can also verify that the
mount point is no longer listed in `/proc/mounts`.

## Interception Library

An interception library called `libioil` is available to work with DFuse. This
library works in conjunction with DFuse and allows the interception of POSIX I/O
calls and issue the I/O operations directly from the application context through
`libdaos` without any application changes.  This provides kernel-bypass for I/O data,
leading to improved performance.

### Using libioil

To use the interception library, set `LD_PRELOAD` to point to the shared library
in the DAOS install directory:

```
LD_PRELOAD=/path/to/daos/install/lib/libioil.so
LD_PRELOAD=/usr/lib64/libioil.so # when installed from RPMs
```

For instance:

```
$ dd if=/dev/zero of=./foo bs=1G count=20
20+0 records in
20+0 records out
21474836480 bytes (21 GB, 20 GiB) copied, 14.1946 s, 1.5 GB/s

$ LD_PRELOAD=/usr/lib64/libioil.so dd if=/dev/zero of=./bar bs=1G count=20
20+0 records in
20+0 records out
21474836480 bytes (21 GB, 20 GiB) copied, 5.0483 s, 4.3 GB/s
```

Alternatively, it's possible to simply link the interception library into the application
at compile time with the `-lioil` flag.

### Monitoring Activity

The interception library is intended to be transparent to the user, and no other
setup should be needed beyond the above.  However this can mean it's not easy to
tell if it is linked correctly and working or not, to detect this you can turn
on reporting of activity by the interception library via environment variable, in which
will case it will print reports to stderr.

If the `D_IL_REPORT` environment variable is set then the interception library will
print a short summary in the shared library destructor, typically as a program
exits, if you set this to a number then it will also log the first read and write
calls as well.  For example, if you set this to a value of 2 then the interception
library will print to stderr on the first two intercepted read calls, the first
two write calls and the first two stat calls.  To have all calls printed set the
value to -1.  A value of 0 means to print the summary at program exit only.

```
D_IL_REPORT=2
```

For instance:

```
$ D_IL_REPORT=1 LD_PRELOAD=/usr/lib64/libioil.so dd if=/dev/zero of=./bar bs=1G count=20
[libioil] Intercepting write of size 1073741824
20+0 records in
20+0 records out
21474836480 bytes (21 GB, 20 GiB) copied, 5.17297 s, 4.2 GB/s

$ D_IL_REPORT=3 LD_PRELOAD=/usr/lib64/libioil.so dd if=/dev/zero of=./bar bs=1G count=5
[libioil] Intercepting write of size 1073741824
[libioil] Intercepting write of size 1073741824
[libioil] Intercepting write of size 1073741824
5+0 records in
5+0 records out
5368709120 bytes (5.4 GB, 5.0 GiB) copied, 1.27362 s, 4.2 GB/s

$ D_IL_REPORT=-1 LD_PRELOAD=/usr/lib64/libioil.so dd if=/dev/zero of=./bar bs=1G count=5
[libioil] Intercepting write of size 1073741824
[libioil] Intercepting write of size 1073741824
[libioil] Intercepting write of size 1073741824
[libioil] Intercepting write of size 1073741824
[libioil] Intercepting write of size 1073741824
5+0 records in
5+0 records out
5368709120 bytes (5.4 GB, 5.0 GiB) copied, 1.29935 s, 4.1 GB/s
```

!!! note
    Some programs, most GNU utilities from the 'coreutils' package have a destructor
    function to close stderr on exit, so for many basic commands such as cp and cat
    whilst the interception library will work it is not possible to see the summary
    generated by the interception library.

### Advanced Usage

DFuse will only create one kernel level mount point regardless of how it is
launched. How POSIX containers are represented within that mount point varies
depending on the DFuse command-line options. In addition to mounting a single
POSIX container, DFuse can also operate in two other modes detailed below.

#### Pool Mode

If a pool uuid is specified but not a container uuid, then the containers can be
accessed by the path `<mount point>/<container uuid>`. The container uuid
will have to be provided from an external source.

```bash
$ daos cont create tank --label mycont --type POSIX
   Container UUID : 8a8f08bb-5034-41e8-b7ae-0cdce347c558
   Container Label: mycont
   Container Type : POSIX
 Successfully created container 8a8f08bb-5034-41e8-b7ae-0cdce347c558

$ daos cont create tank --label mycont2 --type POSIX
  Container UUID : 0db21789-5372-4f2a-b7bc-14c0a5e968df
  Container Label: mycont2
  Container Type : POSIX

Successfully created container 0db21789-5372-4f2a-b7bc-14c0a5e968df

$ dfuse -m /tmp/dfuse --pool tank

$ ls -l /tmp/dfuse/
ls: cannot open directory '/tmp/dfuse/': Operation not supported

$ ls -l /tmp/dfuse/0db21789-5372-4f2a-b7bc-14c0a5e968df
total 0

$ ls -l /tmp/dfuse/8a8f08bb-5034-41e8-b7ae-0cdce347c558
total 0
-rw-rw-r-- 1 jlombard jlombard 0 Jul 10 20:23 foo

$ fusermount3 -u /tmp/dfuse/
```

#### System Mode

If neither a pool or container is specified, then pools and container can be
accessed by the path `<mount point>/<pool uuid>/<container uuid>`. However it
should be noted that `readdir()` and therefore `ls` do not work on either mount
points or directories representing pools here. So the pool and container uuids
will have to be provided from an external source.

```bash
$ dfuse -m /tmp/dfuse
$ df -h /tmp/dfuse
Filesystem      Size  Used Avail Use% Mounted on
dfuse              -     -     -    - /tmp/dfuse
$ daos pool query tank | grep -- -.*-
Pool 004abf7c-26c8-4cba-9059-8b3be39161fc, ntarget=32, disabled=0, leader=0, version=1
$ ls -l /tmp/dfuse/004abf7c-26c8-4cba-9059-8b3be39161fc/0db21789-5372-4f2a-b7bc-14c0a5e968df
total 0
$ ls -l /tmp/dfuse/004abf7c-26c8-4cba-9059-8b3be39161fc/8a8f08bb-5034-41e8-b7ae-0cdce347c558
total 0
-rw-rw-r-- 1 jlombard jlombard 0 Jul 10 20:23 foo
```

While this mode is not expected to be used directly by users, it is useful for
the unified namespace integration.
