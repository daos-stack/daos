# Collective and RPC Transport (CaRT)

> :warning: **Warning:** CaRT is under heavy development. Use at your own risk.

CaRT is an open-source RPC transport layer for Big Data and Exascale HPC. It supports both traditional P2P RPC delivering and collective RPC which invokes the RPC at a group of target servers with a scalable tree-based message propagating.

## License

CaRT is open-sourced software licensed under the Apache License Version 2.0. Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for more information.

## Build

CaRT requires a C99-capable compiler and the scons build tool to build.

CaRT depends on some third-party libraries:
- Mercury (https://github.com/mercury-hpc/mercury) and CCI (https://github.com/CCI/cci) for RPC and underneath communication
  The CCI needs to be patched ("patch -p1 < xxx" to apply all patches in utils/cci), can set the CCI_CONFIG environment variable as your cci config file (an example can be found in "utils/cci/cci.ini")
  The mercury uses openpa (http://git.mcs.anl.gov/radix/openpa.git) for atomic operation.
- PMIx (https://github.com/pmix/master)
  The PMIx uses hwloc library (wget https://www.open-mpi.org/software/hwloc/v1.11/downloads/hwloc-1.11.2.tar.gz).
- Openmpi runtime environment
  The ompi needs to be compiled with the external PMIx/hwloc (an example configuration is "./configure --with-pmix=/your_pmix_install_path / --with-hwloc=/your_hwloc_install_path --with-libevent=external").

Can execute "scons" in top source directory to build it when all the dependent modules installed.
