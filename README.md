# Distributed Asynchronous Object Storage (DAOS)

> :warning: **Warning:** DAOS is under heavy development. Use at your own risk.

DAOS is an open-source storage stack designed for Big Data and Exascale HPC. It provides transactional object storage supporting multi-version concurrency control and is built from the ground up to exploit persistent memory and integrated fabrics. The DAOS stack abstracts multi-tier storage architecture and offers a unified storage model over which multiple top-level APIs are being developed.

## Motivation

The emergence of data-intensive applications in business, government and academia stretches the existing I/O models beyond limits. Modern I/O workloads feature an increasing proportion of metadata combined with misaligned and fragmented data. Conventional storage stacks deliver poor performance for these workloads by adding a lot of latency and introducing alignment constraints. The advent of affordable large-capacity persistent memory combined with an integrated fabric offers a unique opportunity to redefine the storage paradigm and support modern I/O workloads efficiently.

This revolution requires a radical rethink of the complete storage stack. To unleash the full potential of this new technology, the new stack must embrace byte-granular shared-nothing interface from the ground up and be able to support massively distributed storage for which failure will be the norm, while preserving low latency and high bandwidth access over the fabric.

DAOS is a complete I/O architecture that aggregates persistent memory distributed across the fabric into globally-accessible object address spaces, providing consistency, availability and resiliency guarantees without compromising performance.

## License

DAOS is open-sourced software licensed under the Apache License Version 2.0. Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for more information.

## Build

DAOS requires a C99-capable compiler and the scons build tool to build.

DAOS depends on some third-party libraries:
- Mercury (https://github.com/mercury-hpc/mercury) and CCI (wget http://cci-forum.com/wp-content/uploads/2015/12/cci-0.3.0.tar.gz) for RPC and underneath communication
  The CCI needs to be patched ("patch -p1 < xxx" to apply all patches in utils/cci), can set the CCI_CONFIG environment variable as your cci config file (an example can be found in "utils/cci/cci.ini")
  The mercury uses openpa (http://git.mcs.anl.gov/radix/openpa.git) for atomic operation.
- MCL and PMIx (https://github.com/pmix/master) for collective communication
  The PMIx uses hwloc library (wget https://www.open-mpi.org/software/hwloc/v1.11/downloads/hwloc-1.11.2.tar.gz).
- Openmpi runtime environment
  The ompi needs to be compiled with the external PMIx/hwloc used by MCL and PMIx (an example configuration is "./configure --with-pmix=/your_pmix_install_path / --with-hwloc=/your_hwloc_install_path --with-libevent=external").
- NVML library (https://github.com/pmem/nvml.git) for thread-local persistent memory transaction

Can execute "scons" in top DAOS source directory to build it when all the dependent modules installed.
