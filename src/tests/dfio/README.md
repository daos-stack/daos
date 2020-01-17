# DAOS File System FIO Plugin

This plugin allows user to run fio on top of DAOS by adding DAOS file system
(DFS) as an external I/O engine to fio. I/O requests are sent to the
user-specified DAOS pool and container. Rhis plugin is still under development.

## Build

- Modify *FIO_DIR* and *DAOS_DIR* in the Makefile to the install directory of
  fio and DAOS, respectively.
- make

## Usage

See `example.fio` for example:

    daos_pool=edd87892-ab5d-415b-972c-9e035fb3ffed // pool uuid
    daos_cont=5c05a4bf-7299-479c-a617-6a251d1c4ed8 // container uuid
    daos_svcl=0                                    // replicated service list
    daos_chsz=1048576                              // chunk size in bytes (default: 1MiB)

These options can be specified in either the `[global]` section or the
individual job sections of the job file. However, they do have different
implications in the case of multiple fio jobs. See below.
