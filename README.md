# Distributed Asynchronous Object Storage (DAOS)

> :warning: **Warning:** DAOS is under heavy development. Use at your own risk.

DAOS is an open-source storage stack designed for Big Data and Exascale HPC. It provides transactional object storage supporting multi-version concurrency control and is built from the ground up to exploit persistent memory and integrated fabrics. The DAOS stack abstracts multi-tier storage architecture and offers a unified storage model over which multiple top-level APIs are being developed.

## Motivation

The emergence of data-intensive applications in business, government and academia stretches the existing I/O models beyond limits. Modern I/O workloads feature an increasing proportion of metadata combined with misaligned and fragmented data. Conventional storage stacks deliver poor performance for these workloads by adding a lot of latency and introducing alignment constraints. The advent of affordable large-capacity persistent memory combined with an integrated fabric offers a unique opportunity to redefine the storage paradigm and support modern I/O workloads efficiently.

This revolution requires a radical rethink of the complete storage stack. To unleash the full potential of this new technology, the new stack must embrace byte-granular shared-nothing interface from the ground up and be able to support massively distributed storage for which failure will be the norm, while preserving low latency and high bandwidth access over the fabric.

DAOS is a complete I/O architecture that aggregates persistent memory distributed across the fabric into globally-accessible object address spaces, providing consistency, availability and resiliency guarantees without compromising performance.

## License

DAOS is open-sourced software licensed under the Apache License Version 2.0. Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for more information.
