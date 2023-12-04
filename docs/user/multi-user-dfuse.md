# Multi-user dfuse.

## Overview

Multi user dfuse is an operating mode of dfuse where it is able to provide filesystem access to
multiple users/groups on a node through the same dfuse daemon.  This can be used persistently on a
node to provide data sharing and scratch-style access or on-demand by specific users to allow others
to access their data.

### Setup

To enable multi user dfuse libfuse needs to be reconfigured as root to allow other users to access
the mount point.  This is a per-node setting to a configuration file in /etc and will apply for all
users on a node.  Only root can enable this, but once set it applies to all users.

## Recommended use-case

Persistent multi user dfuse instances make sense on login nodes to allow permanent access to DAOS
for multiple users without the overhead of spinning up per-user instances of dfuse on login. It
allows better use of kernel resources, is easier to administer and more flexibile than per-user
dfuse processes.

To set this up it is assumed that the system admins will create a dedicated unix account for this
and configure systemd on login/compile nodes to start dfuse as part of the boot sequence for the
nodes.  This dedicated user should have a pool and container that are owned by them to serve as the
"root" of the dfuse mount, and systemd should be configured to provide these details to dfuse at
startup.  It is anticipated that this root container will serve only to host links to other
containers owned by different users and will not itself contain significant data.

## Unified Namespace.

### Requirement for setting of ACLs.

To make use of Unified Namespace with a single dfuse instance across processes for multiple users
appropriate ACLs need to be set on both DAOS pools and containers.  DFuse is like any other daos
client in this regard and when running multi-user dfuse then it is likely that the user dfuse is
running as does not own all the pools and containers it will be serving.

All pools whose containers are served through this multi-user dfuse mount need a pool ACL that
grants 'r' permissions to the user that is running the multi-user dfuse process. The administrator
may choose to set this when creating pools.

It is possible for a user to run multi-user dfuse and to do so purely to expose data in their own
containers to other users. In this case the user running dfuse is the same as the user owning the
containers so no modification of ACLs is required.

### Automatic setting of ACLs.

For containers the 'r', 't' and 'w' permissions are required on the container for the user running
dfuse.  When containers are created though the `daos` command using a path which resides in a
multi-user dfuse mount then these ACLs are set automatically and this is reported to the user.

If a container is created using a path not backed by multi-user dfuse - for example, on a per-user
dfuse instance or where a `path` option is not provided at all - then in order for anyone to access
the container the user will need to add this permission themselves.  This includes the user that
owns the pool. Without the dfuse user being able to access the container it cannot serve the
contents, even to the container owner.  This applies equally well to containers where the Unified
Namespace link is created after the container itself is created.


## Controlling data sharing and access.

DFuse itself does not handle permissions checks regardless of options but rather provides file and
directory ownership and permissions bits information to the kernel which then checks if user is
entitled to perform the requested operation.  As such once a multi-user dfuse instance is running it
will serve to the kernel the contents of any containers that the dfuse instance itself can access,
and the kernel will then use standard Unix file/directory permissions to decide if operations are
permitted.  It is therefore possible for users be able to access POSIX data across containers
seamlessly where permissions is granted only at the POSIX file/directory layer.

Consider the following example: dfuse is configured to run in the recommended manner, with two users
Anthony and Berlinda who use it to share data, yet Berlinda does not have ACL read access
Anthony's pool or container - only POSIX permissions.

Example:

### Setup user to serve dfuse and create containers, mount points etc.
```
$ sudo -u dserve dmg pool create root_pool --size 1g
$ sudo -u dserve daos cont create --type POSIX root_pool root_container
$ sudo mkdir /daos
$ sudo chown dserve.dserve /daos
```

### Run dfuse, this should be done via systemd to be automatically mounted at boot time.
```
$ sudo -u dserve dfuse --multi-user /daos root_pool root_container
```

### Create a directory for anthony to own, and create a pool for him.
```
$ sudo mkdir -m 0700 /daos/anthony
$ sudo chown anthony.anthony /daos/anthony
$ sudo dmg pool create -u anyhony -g anthony anthony_pool --size 1g
$ sudo dmg pool update-acl anthony_pool -e "A::dserve@:r"
$ sudo dmg pool get-acl anthony_pool
# Owner: anyhony@
# Owner Group: anthony@
# Entries:
A::OWNER@:rw
A::dserve@:r
A:G:GROUP@:rw
$ sudo -u anthony daos cont create --path /daos/anthony/my-data anthony_pool my-data --type POSIX
$ sudo -u anthony daos container get-acl --path /daos/anthony/my-data
# Owner: anthony@
# Owner Group: anthony@
# Entries:
A::OWNER@:rwdtTaAo
A::dserve@:rwt
A:G:GROUP@:rwtT
$ sudo -u anthony chmod 755 /daos/anthony
$ sudo -u anthony sh -c "echo hello-world > /daos/anthony/my-data/new-file"
```

### Now read the file.
```
$ sudo -u berlinda cat /daos/anthony/my-data/new-file
```

## Interception library

Use of the interception library with multi-user dfuse is supported with no change in configuration.
However, in the case where dfuse and the interception library are being run as different users then
the DAOS pool/container handles will not be shared across process boundaries.  In this case the
interception library will itself call pool connect and container open leading to greater overhead in
opening files.  This change should be seamless to the user.
