# Distributed Asynchronous Object Storage (DAOS)

DAOS is a complete I/O architecture that aggregates persistent memory distributed across the fabric into globally-accessible object address spaces, providing consistency, availability and resiliency guarantees without compromising performance.

## Motivation

The emergence of data-intensive applications in business, government and academia stretches the existing I/O models beyond limits. Modern I/O workloads feature an increasing proportion of metadata combined with misaligned and fragmented data. Conventional storage stacks deliver poor performance for these workloads by adding a lot of latency and introducing alignment constraints. The advent of affordable large-capacity persistent memory combined with an integrated fabric offers a unique opportunity to redefine the storage paradigm and support modern I/O workloads efficiently.

This revolution requires a radical rethink of the complete storage stack. To unleash the full potential of this new technology, the new stack must embrace byte-granular shared- nothing interface from the ground up and be able to support massively distributed storage for which failure will be the norm, while preserving low latency and high bandwidth access over the fabric.

## License

DAOS is open-sourced software licensed under the Apache License Version 2.0. Please see the LICENSE & NOTICE files for more information.
