# Distributed Asynchronous Object Storage

[![Build Status](https://travis-ci.org/daos-stack/daos.svg?branch=master)](https://travis-ci.org/daos-stack/daos)

> :warning: **Warning:** DAOS is under heavy development. Use at your own risk.

## What is DAOS?

The Distributed Asynchronous Object Storage (DAOS) stack provides a new storage paradigm for Exascale computing and Big Data. DAOS is an open-source storage stack designed from the ground up to exploit NVRAM and NVMe storage technologies with integrated fabric. It provides ultra-fine grained I/O by using a persistent memory storage model for byte-granular data & metadata combined with NVMe storage for bulk data, all this with end-to-end OS bypass to guarantee ultra-low latency. The DAOS stack aims at increasing data velocity by several orders of magnitude over conventional storage stacks and providing extreme scalability and resilience.

The essence of the DAOS storage model is a key-value store interface over which specific data models can be implemented. A DAOS object is effectively a table of records that are addressed through a flexible multi-level key allowing fine-grain control over colocation of related data. Objects are collected into manageable units called containers. DAOS provides scalable distributed transactions across all objects of a container guaranteeing data consistency and automated rollback on failure to I/O middleware libraries and applications. The DAOS transaction mechanism supports complex workflows with native producer/consumer pipeline in which concurrent consumers do not block producers and consumers receive notification on complete atomic updates and see a consistent snapshot of data..

For both performance and resilience, DAOS objects support multiple distribution and redundancy schemas with fully automated and distributed recovery on storage failure. DAOS uses declustered replication and/or erasure coding over homogeneous shared-nothing servers and provides a lockless consistency model at arbitrary alignment.

Finally, the DAOS stack abstracts multi-tier storage architecture and offers a unified storage model over which multiple top-level APIs will be developed. This includes domain-specific APIs like HDF5, SCiDB, ADIOS and high-level data models like HDFS, Spark and Graph A. A POSIX namespace encapsulation inside a DAOS container is also under consideration.

## Project History

The project started back in 2012 with the Fast Forward Storage & I/O program supported by the U.S. DoE in which a first DAOS prototype was implemented over the Lustre filesystem and ZFS. In 2015, a follow-on program called Extreme Scale Storage and I/O (ESSIO) continued the momentum with the development of a new standalone prototype fully in userspace that is the code base provided in this repository.

## Motivations

The emergence of data-intensive applications in business, government and academia stretches the existing I/O models beyond limits. Modern I/O workloads feature an increasing proportion of metadata combined with misaligned and fragmented data. Conventional storage stacks deliver poor performance for these workloads by adding a lot of latency and introducing alignment constraints. The advent of affordable large-capacity persistent memory combined with an integrated fabric offers a unique opportunity to redefine the storage paradigm and support modern I/O workloads efficiently.

This revolution requires a radical rethink of the complete storage stack. To unleash the full potential of this new technology, the new stack must embrace byte-granular shared-nothing interface from the ground up and be able to support massively distributed storage for which failure will be the norm, while preserving low latency and high bandwidth access over the fabric.

DAOS is a complete I/O architecture that aggregates persistent memory and NVMe storage distributed across the fabric into globally-accessible object address spaces, providing consistency, availability and resiliency guarantees without compromising performance.

## License

DAOS is open-sourced software licensed under the Apache License Version 2.0. Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for more information.

## Reporting Problems

Please report any bugs through our [issue tracker](https://jira.hpdd.intel.com/projects/DAOS) with a test case to reproduce the issue (when applicable) and [debug logs](./doc/debugging.md) that should be compressed.

## Contacts

For more information on DAOS, please post to our [user forum](https://daos.groups.io/g/users).

## Documentations

User documentation:
- [DAOS overview](./doc/overview.md)
- [I/O middleware ported to the native DAOS API](./doc/middleware.md)
- [man pages](./doc/man/man/man3)

Operation manual:
- [building, installing and running DAOS](./doc/quickstart.md)
- [testing DAOS](./doc/testing.md)
- [debugging DAOS](./doc/debugging.md)

Developer zone:
- [contributing to DAOS](./doc/contributing.md)
- [DAOS coding rules](./doc/coding.md)
- [DAOS internals](./doc/internals.md)

The [DAOS wiki](https://wiki.hpdd.intel.com/display/DC/DAOS+Community+Home) includes more [external resources](https://wiki.hpdd.intel.com/display/DC/Resources).
