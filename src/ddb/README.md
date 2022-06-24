# DAOS Debug Tool (ddb)

## Description

The DAOS Debug Tool (ddb) allows a user to navigate through a file in the VOS
format. It is similar to debugfs for ext2/3/4 and offers both a command line and
interactive shell mode.

## Design

DDB will be developed on top of the VOS API which already supports interacting
with a VOS file. The vos_iterate api is heavily used to iterate and navigate
over a VOS tree. Function tables/pointers are used quite a bit as well for
injecting callbacks to vos_iterate which already uses a callback approach. It
also helps support unit testing of the different layers.

### Layers

The primary layers for the application are:

#### Main/Entry Point

The main function in ddb_entry.c initializes daos and vos and accepts program
arguments. It then passes those arguments without any parsing to the ddb_main
function in ddb_main.c which will then parse the arguments and options into
appropriate fields within the main program structure.

#### ddb_main

A unit testable "main" function. It accepts the traditional argc/argv parameters
as well as a function table to input and output functions. Unit testing is then
able to fake the input/output to verify that command line arguments, or
arguments in the interactive mode, run the program in the correct mode and
execute the correct sub command. For the most part, ddb_main() manages the
shell, determining if a command should be run and then the program quit (-R),
run a sequence of commands from a file (-f), or run in interactive mode.

#### ddb commands (sub commands)

The implementation of the individual commands that a user can pass in. It
receives a command's options/arguments as a well defined structure (fields of
which are set by ddb). It interacts with a ddb/vos adapter layer for using the
VOS api.

### ddb vos (dv_)

This layer will adapt the needs of the ddb commands to the current VOS API
implementation, making the VOS interaction a bit nicer for ddb.

# Help and Usage

```
$ ddb -h
The DAOS Debug Tool (ddb) allows a user to navigate through and modify
a file in the VOS format. It offers both a command line and interactive
shell mode. If the '-R' or '-f' options are not provided, then it will
run in interactive mode. In order to modify the file, the '-w' option
must be included.

Many of the commands take a vos tree path. The format for this path
is [cont]/[obj]/[dkey]/[akey]/[recx]. The container is the container
uuid. The object is the object id.  The keys parts currently only
support string keys and must be surrounded with a single quote (') unless
using indexes (explained later). The recx for array values is the
format {lo-hi}. To make it easier to navigate the tree, indexes can be
used instead of the path part. The index is in the format [i]

Usage:
ddb [path] [options]

    [path]
	The path to the vos file to open. This should be an absolute
	path to the pool shard. Part of the path is used to
	determine what the pool uuid is. If a path is not provided
	initially, the open command can be used later to open the
	vos file.

Options:
   -w, --write_mode
	Open the vos file in write mode. This allows for modifying
	the vos file with the load,
	commit_ilog, etc commands.
   -R, --run_cmd <cmd>
	Execute the single command <cmd>, then exit.
   -f, --file_cmd <path>
	The path to a file container a list of ddb commands, one
	command per line, then exit.
   -h, --help
	Show tool usage.
Commands:
   help              Show help message for all the commands.
   quit              Quit interactive mode
   ls                List containers, objects, dkeys, akeys, and values
   open              Opens the vos file at <path>
   close             Close the currently opened vos pool shard
   dump_superblock   Dump the pool superblock information
   dump_value        Dump a value to a file
   rm                Remove a branch of the VOS tree.
   load              Load a value to a vos path.
   dump_ilog         Dump the ilog
   commit_ilog       Process the ilog
   rm_ilog           Remove all the ilog entries
   dump_dtx          Dump the dtx tables
   clear_cmt_dtx     Clear the dtx committed table
   smd_sync          Restore the SMD file with backup from blob

```

```
$ ddb -R help
help
	Show help message for all the commands.

quit
	Quit interactive mode

ls [path]
	List containers, objects, dkeys, akeys, and values
    [path]
	Optional, list contents of the provided path
Options:
    -r, --recursive
	Recursively list the contents of the path

open <path>
	Opens the vos file at <path>
    <path>
	The path to the vos file to open. This should be an absolute
	path to the pool shard. Part of the path is used to
	determine what the pool uuid is.
Options:
    -w, --write_mode
	Open the vos file in write mode. This allows for modifying
	the vos file with the load, commit_ilog, etc commands.

close
	Close the currently opened vos pool shard

dump_superblock
	Dump the pool superblock information

dump_value <path> <dst>
	Dump a value to a file
    <path>
	The vos tree path to dump. Should be a complete path
	including the akey and if the value is an array value it
	should include the extent.
    <dst>
	The file path to dump the value to.

rm <path>
	Remove a branch of the VOS tree. The branch can be anything
	from a container and everything under it, to a single value.
    <path>
	The vos tree path to remove.

load <src> <dst>
	Load a value to a vos path. This can be used to update the
	value of an existing key, or create a new key.
    <src>
	The source file path that contains the data for the value to
	load.
    <dst>
	The destination vos tree path to the value where the data
	will be loaded. If the path currently exists, then the
	destination path must match the value type, meaning, if the
	value type is an array, then the path must include the recx,
	otherwise, it must not.

dump_ilog <path>
	Dump the ilog
    <path>
	The vos tree path to an object, dkey, or akey.

commit_ilog <path>
	Process the ilog
    <path>
	The vos tree path to an object, dkey, or akey.

rm_ilog <path>
	Remove all the ilog entries
    <path>
	The vos tree path to an object, dkey, or akey.

dump_dtx <path>
	Dump the dtx tables
    <path>
	The vos tree path to a container.
Options:
    -a, --active
	Only dump entries from the active table
    -c, --committed
	Only dump entries from the committed table

clear_cmt_dtx <path>
	Clear the dtx committed table
    <path>
	The vos tree path to a container.

smd_sync
	Restore the SMD file with backup from blob


```