# DAOS: a Scale-out Key-Array Object Store

[![Build Status](https://travis-ci.org/daos-stack/daos.svg?branch=master)](https://travis-ci.org/daos-stack/daos)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](./LICENSE)

> :warning: **Warning:** DAOS is under heavy development. Use at your own risk.

## What is DAOS?

The **D**istributed **A**synchronous **O**bject **S**torage stack provides a new storage paradigm for Exascale computing and Big Data. DAOS is an open-source storage stack designed from the ground up to exploit NVRAM and NVMe storage technologies. It provides fine grained I/O by using a persistent memory storage model for byte-granular data & metadata combined with NVMe storage for bulk data, all this with end-to-end OS bypass to guarantee high IOPS. The DAOS stack aims at increasing data velocity by several orders of magnitude over conventional storage stacks and providing extreme scalability and resilience.

The essence of the DAOS storage model is a key-array store interface designed to store efficiently both structured and unstructured data and over which specific data models can be implemented. A DAOS object is effectively an array of records that are addressed through a flexible multi-level key allowing fine-grain control over colocation of related data. Objects are collected into manageable units called containers. DAOS provides scalable distributed transactions across all objects of a container guaranteeing data consistency and automated rollback on failure to I/O middleware libraries and applications. The DAOS transaction mechanism supports complex workflows with native producer/consumer pipeline in which concurrent consumers do not block producers and consumers receive notification on complete atomic updates and see a consistent snapshot of data..

For both performance and resilience, DAOS objects support multiple distribution and redundancy schemas with fully automated and distributed recovery on storage failure. DAOS uses declustered replication and/or erasure coding over homogeneous shared-nothing servers and provides a lockless consistency model at arbitrary alignment.

Finally, the DAOS stack abstracts multi-tier storage architecture and offers a unified storage model over which multiple top-level APIs will be developed. This includes domain-specific APIs like HDF5, SCiDB, ADIOS and high-level data models like Apache Arrow and Graph A. A POSIX namespace encapsulation inside a DAOS container is also under consideration.

## Project History

The project started back in 2012 with the Fast Forward Storage & I/O program supported by the U.S. DoE in which a first DAOS prototype was implemented over the Lustre filesystem and ZFS. In 2015, a follow-on program called Extreme Scale Storage and I/O (ESSIO) continued the momentum with the development of a new standalone prototype fully in userspace that is the code base provided in this repository.

## License

DAOS is open-sourced software licensed under the Apache License Version 2.0. Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for more information.

## Contacts

For more information on DAOS, please post to our [user forum](https://daos.groups.io/g/users).
Please report any bugs through our [issue tracker](https://jira.hpdd.intel.com/projects/DAOS) with a test case to reproduce the issue (when applicable) and [debug logs](./doc/debugging.md) that should be compressed.

## Documentations

User materials:
* [DAOS overview](./doc/overview.md)
* [I/O middleware libraries ported to the native DAOS API](./doc/middleware.md)
* [DAOS API](./src/include/) and [man pages](./doc/man/man3)

Operation manual:
* [building, installing and running DAOS](./doc/quickstart.md)
* [testing DAOS](./doc/testing.md)
* [debugging DAOS](./doc/debugging.md)

Developer zone:
* [contributing to DAOS](./doc/contributing.md)
* [DAOS coding rules](./doc/coding.md)
* [DAOS internals](./doc/internals.md)

The [DAOS wiki](https://wiki.hpdd.intel.com/display/DC/DAOS+Community+Home) includes more [external resources](https://wiki.hpdd.intel.com/display/DC/Resources).
