# DFS Mapping

DFS stands for DAOS File System. The DFS API provides an encapuslated namespace
with a POSIX like API directly on top of the DAOS API. The namespace is
encapsulated under a single DAOS container where directories and files are
objects in that container.

The encapsulated namespace will be located in one DAOS Pool and a single DAOS
Container. The user provides a valid (connected) pool handle and an open
container handle where the namespace will be located.

## Namespace

When the file system is created (i.e. when the DAOS container is initialized as
an encapsulated namespace), a reserved object (with a predefined object ID) will
be added to the container and will record superblock information about the
namespace. This will include the root directory, which will be another reserved
object and will be the same representation as a directory. The oid of the root
id will be inserted as an entry in the superblock object.

The DAOS container that is hosting the encapsulated namespace will contain the
superblock object with a reserved oid (0.0) and with object class
DAOS_OC_REPL_MAX_RW. This object contains an entry with a magic value to
indicate it's a POSIX filesystem. The SB object will contain also an entry to
the root directory of the filesystem with oid (0.1) and object class
DAOS_OC_REPL_MAX_RW. The SB will look like this:

~~~~
D-key: "DFS_SB_DKEY"
A-key: "DFS_SB_AKEY"
single-value: SB_MAGIC (0xda05df50da05df50)

D-key: "/"
// rest of akey entries for root are same as in directory entry described below.
~~~~~~

## Directories:

A POSIX directory will map to a DAOS object with multiple dkeys, where each dkey
will correspond to an entry in that directory (for another subdirectory, regular
file, or symbolic link). The dkey value will be the entry name in that
directory. The dkey will contain several akeys of type DAOS_IOD_SINGLE (single
value), where each akey contains an attribute of that entry. The mapping table
will look like this (includes two extended attributes: xattr1, xattr2):

~~~~~~
Directory Object
  D-key “entry1_name”
    A-key “mode”	// mode_t (permission bit mask + type of entry)
    A-key “oid”		// object id of entry (bogus if symlink)
    A-key “value”	// symlink value (akey does not exist if not a symlink)
    A-key “uid”		// user id
    A-key “gid”		// group id
    A-key “atime”	// access time
    A-key “mtime”	// modify time
    A-key “ctime”	// change time
    A-key “xattr1”	// extended attribute name (if any)
    A-key “xattr2”	// extended attribute name (if any)
~~~~~~

This summarizes the mapping of a directory testdir with a file, directory, and
symlink:

~~~~~~
testdirdir$ ls
dir1
file1
syml1 -> dir1

Object testdir
  D-key “dir1”
    A-key “mode” , permission bits + S_IFDIR
    A-key “oid” , object id of dir1
    …
  D-key “file1”
    A-key “mode” , permission bits + S_IFREG
    A-key “oid” , object id of file1
    …
  D-key “syml1”
    A-key “mode” , permission bits + S_IFLNK
    A-key “oid” , empty
    A-key “value”, dir1
    …
~~~~~~

For files, we can have an optimization in the entry by storing the first 4K of
data in the entry itself under another akey “data” for the file entry. In this
case, if the file size is less than or equal to 4K, the object ID akey will be
empty, and the file data will be in the akey with array type of file_size
records. Otherwise the akey “data” will be empty and the “oid” akey will contain
a valid object ID for the file data.

Note that with this mapping, the inode information is stored with the entry that
it corresponds to in the parent directory object. Thus, hard links won’t be
supported, since it won’t be possible to create a different entry (dkey) that
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
chunk_size elements. The Object properties are stored under the first dkey (0)
in a special akey. So for example, if we have an array of 10 elements, and chunk
size is 3 elements, the array object will contain the following:

~~~~
Object array
  D-key 0
    A-key “daos_array_metadata”
    array value with 3 uint64_t records: magic value, element size, chunk size
    A-key 0 , array elements [0,1,2]
  D-key 1
    A-key 1 , array elements [3,4,5]
  D-key 2
    A-key 2 , array elements [6,7,8]
  D-key 3
    A-key 3 , array elements [9]
~~~~~~

Access to that object is done through the DAOS Array API. All read and write
operations to the file will be translated to DAOS array read and write
operations. The file size can be set (truncate) or retrieved by the DAOS array
set/get_size functions. Increasing the file size however in this case, does not
guarantee that space is allocated. Since DAOS logs I/Os across different epoch,
space allocation cannot be supported by a naïve set size operation.

## Symbolic Links:

As mentioned in the directory section, symbolic links will not have an object
for the symlink itself, but will have a value in the entry itself of the parent
directory containing the actual value of the symlink.
