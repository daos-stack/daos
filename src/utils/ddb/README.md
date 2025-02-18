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

Command line help:

```
$ ddb -h
# ...
```

Interactive shell mode help:

```
$ help

# ...
```