# DAOS
[![Build Status](https://travis-ci.org/daos-stack/daos.svg?branch=master)](https://travis-ci.org/daos-stack/daos)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](./LICENSE)
[![Codacy Badge](https://api.codacy.com/project/badge/Grade/4163f52ec65e4ba8991208288a9a15a6)](https://www.codacy.com/app/johann.lombardi/daos?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=daos-stack/daos&amp;utm_campaign=Badge_Grade)

> :warning: **Warning:** DAOS is under heavy development. Use at your own risk.

## What is DAOS?

The **D**istributed **A**synchronous **O**bject **S**torage (DAOS) is an open-source software-defined object store designed from the ground up for massively distributed Non Volatile Memory (NVM). DAOS takes advantage of next generation NVM technology like Storage Class Memory (SCM) and NVM express (NVMe) while presenting a key-value storage interface and providing features such as transactional non-blocking I/O, advanced data protection with self healing on top of commodity hardware, end-to-end data integrity, fine grained data control and elastic storage to optimize performance and cost.

## License

DAOS is licensed under the Apache License Version 2.0. Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for more information.

## Documentations

#### Administration guide:
The DAOS admin guide is available [online](https://daos-stack.github.io).

#### User area:
* [Overview of the DAOS storage model](doc/storage_model.md)
* DAOS API
  * C [native API](src/include/) and [man pages](doc/man/man3)
  * Python [bindings](src/client/pydaos/)
  * Go language [bindings](https://github.com/daos-stack/go-daos) and [documentation](https://godoc.org/github.com/daos-stack/go-daos/pkg/daos)
* I/O middleware integration
  * [DAOS VOL plugin](https://bitbucket.hdfgroup.org/projects/HDFFV/repos/hdf5/browse?at=refs%2Fheads%2Fhdf5_daosm) for HDF5
  * [ROMIO DAOS ADIO driver](https://github.com/daos-stack/mpich/tree/daos_adio) for MPI-IO
  * [libdfs](src/include/daos_fs.h) implements files and directories over the DAOS API by encapsulating a POSIX namespace into a DAOS container. This library can be linked directly with the application (e.g. see IOR and mdtest [DAOS backend](https://github.com/daos-stack/ior-hpc/tree/daos)) or mounted locally through [FUSE](src/client/dfs/dfuse.c) (one mountpoint per container for now).

#### Developer zone:
* [contributing](https://wiki.hpdd.intel.com/display/DC/Contribute) to DAOS
* [coding rules](https://wiki.hpdd.intel.com/display/DC/Coding+Rules)
* [internals documentation](src/README.md)

More information can be found on the [wiki](https://wiki.hpdd.intel.com/display/DC/DAOS+Community+Home).

## Contacts

For any questions, please post to our [user forum](https://daos.groups.io/g/daos). Bugs should be reported through our [issue tracker](https://jira.hpdd.intel.com/projects/DAOS) with a test case to reproduce the issue (when applicable) and [debug logs](./doc/debugging.md).

Added just to generate a commit to generate RPMs.
