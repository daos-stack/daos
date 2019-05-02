# DFS Overview

DFS stands for DAOS File System. The DFS API provides an encapuslated namespace
with a POSIX like API directly on top of the DAOS API. The namespace is
encapsulated under a single DAOS container where directories and files are
objects in that container.

The encapsulated namespace will be located in one DAOS Pool and a single DAOS
Container. The user provides a valid (connected) pool handle and an open
container handle where the namespace will be located.

## DFS Namespace

When the file system is created (i.e. when the DAOS container is initialized as
an encapsulated namespace), a reserved object (with a predefined object ID) will
be added to the container and will record superblock information about the
namespace. The SB object is replicated and has the reserved OID 0.0.

The SB object contains an entry with a magic value to indicate it's a POSIX
filesystem. The SB object will contain also an entry to the root directory of
the filesystem, which will be another reserved object with a predefined oid
(1.0) and will have the same representation as a directory (see next
section). The oid of the root id will be inserted as an entry in the superblock
object.

The SB will look like this:

~~~~
D-key: "DFS_SB_DKEY"
A-key: "DFS_SB_AKEY"
single-value (uint64_t): SB_MAGIC (0xda05df50da05df50)

D-key: "/"
// rest of akey entries for root are same as in directory entry described below.
~~~~~~

## DFS Directories

A POSIX directory will map to a DAOS object with multiple dkeys, where each dkey
will correspond to an entry in that directory (for another subdirectory, regular
file, or symbolic link). The dkey value will be the entry name in that
directory. The dkey will contain several akeys of type `DAOS_IOD_SINGLE`
(single value), where each akey contains an attribute of that entry. The mapping
table will look like this (includes two extended attributes: xattr1, xattr2):

~~~~~~
Directory Object
  D-key "entry1_name"
    A-key "mode"	// mode_t (permission bit mask + type of entry)
    A-key "oid"		// object id of entry (akey does not exist if symlink)
    A-key "syml"	// symlink value (akey does not exist if not a symlink)
    A-key "atime"	// access time
    A-key "mtime"	// modify time
    A-key "ctime"	// change time
    A-key "x:xattr1"	// extended attribute name (if any)
    A-key "x:xattr2"	// extended attribute name (if any)
~~~~~~

The extended attributes are all prefixed with "x:".

This summarizes the mapping of a directory testdir with a file, directory, and
symlink:

~~~~~~
testdirdir$ ls
dir1
file1
syml1 -> dir1

Object testdir
  D-key "dir1"
    A-key "mode" , permission bits + S_IFDIR
    A-key "oid" , object id of dir1
    ...
  D-key "file1"
    A-key "mode" , permission bits + S_IFREG
    A-key "oid" , object id of file1
    ...
  D-key "syml1"
    A-key "mode" , permission bits + S_IFLNK
    A-key "oid" , empty
    A-key "syml", dir1
    ...
~~~~~~

For files, we will have an optimization in the entry by storing the first 4K of
data in the entry itself under another akey "file_data" for the file entry. In
this case, if the file size is less than or equal to 4K, the object ID akey will
be empty, and the file data will be in the akey with array type of file_size
records. Otherwise the "oid" akey will contain a valid object ID for the file
data.

Note that with this mapping, the inode information is stored with the entry that
it corresponds to in the parent directory object. Thus, hard links won"t be
supported, since it won"t be possible to create a different entry (dkey) that
actually points to the same set of akeys that the current ones are stored
within. This limitation was agreed upon, and makes the representation simple as
described above.

## Files

As shown in the directory mapping above, the entry of a file will be inserted in
its parent directory object with an object ID that corresponds to that file. The
object ID for a regular file will be of a DAOS array object, which itself is a
DAOS object with some properties being the element size and chunk size. In the
POSIX file case, the cell size will always be 1 byte. The chunk size can be set
at create time only, with the default being 1 MB. The array object itself is
mapped onto a DAOS object with integer dkeys, where each dkey contains
chunk_size elements. So for example, if we have a file with size 10 bytes, and
chunk size is 3 bytes, the array object will contain the following:

~~~~
Object array
  D-key 0
    A-key NULL , array elements [0,1,2]
  D-key 1
    A-key NULL , array elements [3,4,5]
  D-key 2
    A-key NULL , array elements [6,7,8]
  D-key 3
    A-key NULL , array elements [9]
~~~~~~

For more information about the array object layout, please refer to the
README.md file for Array Addons.

Access to that object is done through the DAOS Array API. All read and write
operations to the file will be translated to DAOS array read and write
operations. The file size can be set (truncate) or retrieved by the DAOS array
set/get_size functions. Increasing the file size however in this case, does not
guarantee that space is allocated. Since DAOS logs I/Os across different epoch,
space allocation cannot be supported by a naïve set size operation.

## Symbolic Links

As mentioned in the directory section, symbolic links will not have an object
for the symlink itself, but will have a value in the entry itself of the parent
directory containing the actual value of the symlink.

## Access Permissions

All DFS objects (files, directories, and symlinks) inherit the access
permissions of the DFS pool that they are created with. So when a user is trying
to access an object in the DFS namespace, it's real/effective uid/gid are
compared against those of the pool's uid and gid that are obtained when
connecting to the pool. The check then is done with the stored object mode and
depending on the type of access being requested (R, W, X) and the object mode,
access permission is determined. In the source code, this is implemented in the
function `check_access()`.

setuid(), setgid() programs, supplementary groups, ACLs are not supported at the
moment.

## DFUSE_HL

A simple high level fuse plugin (dfuse_hl) is implemented to test the DFS API
and functionality with existing POSIX tests and benchmarks (IOR, mdtest,
etc.). The DFS high level fuse exposes one mounpoint as a single DFS namespace
with a single pool and container.
