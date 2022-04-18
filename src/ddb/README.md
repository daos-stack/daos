# DAOS Debug Tool (ddb)

## Description

The DAOS Debug Tool (ddb) allows a user to navigate through a file in the VOS
format. It is similar to debugfs for ext2/3/4 and offers both a command line and
interactive shell mode. Key features that will be supported are:

### Parsing a VOS file:

- Printing the VOS 'superblock'
- listing containers, objects, dkeys, akeys, and records
- dumping content of a value
- dumping content of ilogs
- iterating over the dtx committed and active tables

### Altering a VOS file, including:

- deleting containers, objects, dkeys, akeys, or records
- changing/inserting new value
- processing/removing ilogs
- clearing the dtx committed table

## Current State

ddb is very much in development. Currently the interactive mode and the '-R'
option with a single command work relatively well. The only commands that are
currently implemented are quit (for interactive mode) and 'ls' to list the
branches of a vos file (container, objects, ...).

## Design

DDB will be developed on top of the VOS API which already supports interacting
with a VOS file. The vos_iterate api is heavily used to iterate and navigate
over a VOS tree. Function tables/pointers are used quite a bit as well for
injecting callbacks to vos_iterate which already uses a callback approach. It
also helps support unit testing of the different layers.

### Layers

The primary layers for the application are:

#### Main/Entry Point

The main function which initializes daos and vos and accepts program arguments.
It then passes those arguments to the ddb_main function which will then parse
the arguments and options into appropriate fields within the main program
structure.

#### ddb_main

A unit testable "main" function. It accepts the traditional argc/argv parameters
as well as a function table to input and output functions. Unit testing is then
able to fake the input/output to verify that command line arguments, or
arguments in the interactive mode, run the program in the correct mode and
execute the correct sub command. A function table is defined for the actual sub
commands so that these can be faked out as well and the cli can be unit tested
in isolation.

#### ddb commands (sub commands)

The implementation of the individual commands that a user can pass in. It
receives the command options/arguments as a well defined structure (fields of
which are set by ddb).

### ddb vos (dv_)

This layer will adapt the needs of the ddb commands to the current VOS API
implementation, making the VOS interaction a bit nicer for ddb.

