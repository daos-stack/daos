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
also helps support unit testing of the different layers. The user interface is
written in golang to support a richer interactive shell and cli experience. The
golang code wraps c functions which do the heavy lifting of the command.

### Layers

The primary layers for the application are:

#### CLI / User interface

The golang interface which handles parsing most of the user input. The
github.com/jessevdk/go-flags module handles the user input from the command
line. This includes determining if the -f option is passed and if a
path to a vos file was supplied.

The github.com/desertbit/grumble module handles the execution of the commands,
whether from interactive mode or from the -f value. It also supplies
the interactive mode, managing history, input keys, etc.

The golang code also calls the c code functions to initialize daos and vos.

#### ddb commands (sub commands)

The implementation of the individual commands that ddb supports. It
receives a command's options/arguments as a well defined structure (fields of
which are set by ddb). It interacts with a ddb/vos adapter layer for using the
VOS api.

### ddb vos (dv_)

This layer will adapt the needs of the ddb commands to the current VOS API
implementation, making the VOS interaction a bit nicer for ddb.

## Help and Usage

```
$ ddb -h
Usage:
  ddb [OPTIONS] [vos_file_path] [ddb_command] [ddb_command_args...]

The DAOS Debug Tool (ddb) allows a user to navigate through and modify
a file in the VOS format. It offers both a command line and interactive
shell mode. If neither a single command or '-f' option is provided, then
the tool will run in interactive mode. In order to modify the VOS file,
the '-w' option must be included. If supplied, the VOS file supplied in
the first positional parameter will be opened before commands are executed.

Many of the commands take a vos tree path. The format for this path
is [cont]/[obj]/[dkey]/[akey]/[extent].
- cont - the full container uuid.
- obj - the object id.
- keys (akey, dkey) - there are multiple types of keys
-- string keys are simply the string value. If the size of the
key is greater than strlen(key), then the size is included at
the end of the string value. Example: 'akey{5}' is the key: akey
with a null terminator at the end.
-- number keys are formatted as '{[type]: NNN}' where type is
'uint8, uint16, uint32, or uint64'. NNN can be a decimal or
hex number. Example: '{uint32: 123456}'
-- binary keys are formatted as '{bin: 0xHHH}' where HHH is the hex
representation of the binary key. Example: '{bin: 0x1a2b}'
- extent for array values - in the format {lo-hi}.

To make it easier to navigate the tree, indexes can be
used instead of the path part. The index is in the format [i]. Indexes
and actual path values can be used together

Example Paths:
/3550f5df-e6b1-4415-947e-82e15cf769af/939000573846355970.0.13.1/dkey/akey/[0-1023]
[0]/[1]/[2]/[1]/[9]
/[0]/939000573846355970.0.13.1/[2]/akey{5}/[0-1023]


Application Options:
      --debug             enable debug output
  -w, --write_mode        Open the vos file in write mode.
  -f, --cmd_file=         Path to a file containing a sequence of ddb commands to execute.
  -p, --db_path=          Path to the sys db.
  -v, --version           Show version

Help Options:
  -h, --help              Show this help message
```

### Interactive mode help

```
$ help

Commands:
  clear    clear the screen
  exit     exit the shell
  help     use 'help [command]' for command help
  quit, q  exit the shell
  version  Print ddb version

smd
  smd_sync  Restore the SMD file with backup from blob

vos
  close                    Close the currently opened vos pool shard
  dev_list                 List all devices
  dev_replace              Replace an old device with a new unused device
  dtx_act_abort            Mark the active dtx entry as aborted
  dtx_act_commit           Mark the active dtx entry as committed
  dtx_act_discard_invalid  Discard the active DTX entry's records if invalid.
  dtx_aggr                 Aggregate DTX entries
  dtx_cmt_clear            Clear the dtx committed table
  dtx_dump                 Dump the dtx tables
  dtx_stat                 Stat on DTX entries
  feature                  Manage vos pool features
  ilog_clear               Remove all the ilog entries
  ilog_commit              Process the ilog
  ilog_dump                Dump the ilog
  ls                       List containers, objects, dkeys, akeys, and values
  open                     Opens the vos file at <path>
  prov_mem                 Prepare the memory environment for md-on-ssd mode
  rm                       Remove a branch of the VOS tree.
  rm_pool                  Remove a vos pool.
  superblock_dump          Dump the pool superblock information
  value_dump               Dump a value
  value_load               Load a value to a vos path.
  vea_dump                 Dump information from the vea about free regions
  vea_update               Alter the VEA tree to mark a region as free.
```

## `prov_mem` command

```
Prepare the memory environment for md-on-ssd mode

Usage:
  prov_mem [flags] db_path tmpfs_mount

Args:
  db_path      string    Path to the sys db.
  tmpfs_mount  string    Path to the tmpfs mountpoint.

Flags:
  -h, --help               display help
  -s, --tmpfs_size uint    Specify tmpfs size(GiB) for mount. By default, the total size of all VOS files will be used.
```

### Description

This command is used when working with DAOS in md-on-ssd (metadata-on-SSD) mode. It:

1. Verifies the system is running in MD-on-SSD mode.
2. Creates a tmpfs mount at the specified path (if not already mounted).
3. Sets up the necessary directory structure.
4. Recreates VOS pool target files on the tmpfs mount.

### Examples

**Note**: Please do not omit the first empty argument.

**Note**: The user you use have to have access to specified resources and be able to mount(2).

```bash
# Prepare memory environment with auto-calculated tmpfs size
ddb "" prov_mem /path/to/sys/db /mnt/tmpfs

# Prepare memory environment with specific tmpfs size of 16 GiB
ddb "" prov_mem -s 16 /path/to/sys/db /mnt/tmpfs
```

### Notes

- The `tmpfs_mount` path must not already be a mount point; otherwise, the command will fail with a "busy" error. 
- If `tmpfs_size` is not specified, the size will be automatically calculated based on the total size of all VOS files. 
- This command requires the system to be configured for MD-on-SSD mode.
