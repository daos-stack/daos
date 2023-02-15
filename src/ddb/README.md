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
line. This includes determining if the -R and -f options are passed and if a
path to a vos file was supplied.

The github.com/desertbit/grumble module handles the execution of the commands,
whether from interactive mode or from the values of -R or -f. It also supplies
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

# Help and Usage

```
$ ddb -h
Usage:
  ddb [OPTIONS] [<vos_file_path>]

The DAOS Debug Tool (ddb) allows a user to navigate through and modify
a file in the VOS format. It offers both a command line and interactive
shell mode. If the '-R' or '-f' options are not provided, then it will
run in interactive mode. In order to modify the file, the '-w' option
must be included. The optional <vos_file_path> will be opened before running
commands supplied by '-R' or '-f' or entering interactive mode.

Application Options:
  -R, --run_cmd=    Execute the single command <cmd>, then exit
  -f, --file_cmd=   Path to a file container a list of ddb commands, one
                    command per line, then exit.
  -w, --write_mode  Open the vos file in write mode.

Help Options:
  -h, --help        Show this help message
```

Interactive mode help
```
$ help

The DAOS Debug Tool (ddb) allows a user to navigate through and modify
a file in the VOS format. In order to modify the file, the '-w' option must
be included when opening the vos file.

Many of the commands take a vos tree path. The format for this path
is 'cont_uuid/obj_id/dkey/akey/recx'. The keys currently only support string
keys. The recx for array values is the format {lo-hi}. To make it easier to
navigate the tree, indexes can be used instead of the path part. The index
is in the format '[i]', for example '[0]/[0]/[0]'

Commands:
  clear            clear the screen
  clear_cmt_dtx    Clear the dtx committed table
  close            Close the currently opened vos pool shard
  commit_ilog      Process the ilog
  dtx_abort        Mark the active dtx entry as aborted
  dtx_commit       Mark the active dtx entry as committed
  dump_dtx         Dump the dtx tables
  dump_ilog        Dump the ilog
  dump_superblock  Dump the pool superblock information
  dump_value       Dump a value to a file
  dump_vea         Dump information from the vea about free regions
  exit             exit the shell
  help             use 'help [command]' for command help
  load             Load a value to a vos path.
  ls               List containers, objects, dkeys, akeys, and values
  open             Opens the vos file at <path>
  rm               Remove a branch of the VOS tree.
  rm_ilog          Remove all the ilog entries
  smd_sync         Restore the SMD file with backup from blob
  update_vea       Alter the VEA tree to mark a region as free.
```