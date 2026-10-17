# Collective and RPC Transport (CaRT)

CaRT is an open-source RPC transport layer for Big Data and Exascale HPC.
It supports both traditional P2P RPC delivering and collective RPC which
invokes the RPC at a group of target servers with a scalable tree-based
message propagating.

## License

CaRT is open source software distributed as part of DAOS under a BSD license.
Please see the [LICENSE](../../LICENSE) & [NOTICE](../../NOTICE) files for
more information.

## Build

CaRT requires a C99-capable compiler and the scons build tool to build.
It is built as part of the DAOS build process.

CaRT depends on some third-party libraries:

- Mercury (https://github.com/mercury-hpc/mercury) for RPC and underneath communication.
  Mercury uses openpa (http://git.mcs.anl.gov/radix/openpa.git) for atomic operation.
- PMIx (https://github.com/pmix/master).
  PMIx uses the hwloc library (wget https://www.open-mpi.org/software/hwloc/v1.11/downloads/hwloc-1.11.2.tar.gz).
- Openmpi runtime environment.
  The ompi needs to be compiled with the external PMIx/hwloc
  (an example configuration is `./configure --with-pmix=/your_pmix_install_path / --with-hwloc=/your_hwloc_install_path --with-libevent=external`).

Can execute `scons` in top source directory to build it when all the dependent modules are installed.
