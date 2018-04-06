# Collective and RPC Transport (CaRT)

> :warning: **Warning:** CaRT is under heavy development. Use at your own risk.

CaRT is an open-source RPC transport layer for Big Data and Exascale HPC. It supports both traditional P2P RPC delivering and collective RPC which invokes the RPC at a group of target servers with a scalable tree-based message propagating.

# Gurt Useful Routines and Types (GURT)

GURT is a open-source library of helper functions and data types. The library makes it easy to manipulate lists, hash tables, heaps and logging.

All Gurt Useful Routines and Types are prefixed with 'd', the 4th letter in the alphabet because gurt useful words have 4 letters.

## License

CaRT is open source software distributed under a BSD license.
Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for
more information.

## Build

CaRT requires a C99-capable compiler and the scons build tool to build.

CaRT depends on some third-party libraries:
- Mercury (https://github.com/mercury-hpc/mercury) for RPC and underneath communication
  The mercury uses openpa (http://git.mcs.anl.gov/radix/openpa.git) for atomic operation.
- PMIx (https://github.com/pmix/master)
  The PMIx uses hwloc library (wget https://www.open-mpi.org/software/hwloc/v1.11/downloads/hwloc-1.11.2.tar.gz).
- Openmpi runtime environment
  The ompi needs to be compiled with the external PMIx/hwloc (an example configuration is "./configure --with-pmix=/your_pmix_install_path / --with-hwloc=/your_hwloc_install_path --with-libevent=external").

Can execute "scons" in top source directory to build it when all the dependent modules installed.
