# DFuse overview

DFuse provides File System access to DAOS through the standard libc/kernel/VFS
POSIX infrastructure.  This allows existing applications to use DAOS without
modification, and provides a path to upgrade those applications to native DAOS
support.  In addition DFuse provides an Interception Library to transparrently
allow POSIX clients to talk directly to DAOS servers providing OS-Bypass for
I/O without modifying of recompiling of the application.

DFuse builds heavily on [DFS](../dfs/README.md) and data written via DFuse can
be access by DFS and vice versa.

## DFuse Deamon

The dfuse daemon runs a single instance per node to provide a user POSIX access
to DAOS, it should be run with the credentials of the user and typically will
be started and stopped on each compute node as part of the prolog and epilog
scripts of any resource manager or scheduler in use.  One dfuse daemon per node
can process requests for multiple clients.

A single dfuse instance can provide access to multiple pools and containers
concurrently, or can be limited to a single pool, or a single container.

## Restrictions

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

It is not possible to see pool/container listings through dfuse so if readdir
or ls etc. are used for this dfuse will return ENOTSUP.

## Launching

dfuse should be run with the credentails (user/group) of the user that will
be accessing it, and that owns any pools that will be used.

There are two mandatory command line options, these are

* --svc=RANKS  <service replicas>
* --mountpount=PATH <path to mount DAOS>

In addition, there are several optional command line options

* --pool=POOL <pool to connect to>
* --container=CONTAINER <container to open>
* --sys-name=NAME <DAOS server name>
* --foreground <run in foreground>
* --singlethreaded <run single threaded>

When dfuse starts it will register a single mount with the kernel at the
location specified by the --mountpoint option, and this mount will be
visable in /proc/mounts and possibly the output of df.  The contents of
multiple pools/containers will be accessible via this single kernel
mountpoint.

## Pool/Container paths.
