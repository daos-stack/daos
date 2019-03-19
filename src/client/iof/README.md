# I/O Forwarding Libraries (IOF)

**Warning:** IOF is under heavy development.  Use at your own risk.

IOF provides I/O forwarding services for off cluster filesystems

## License

IOF is open source software distributed under a BSD license.
Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for
more information.

## Build

IOF requires a c99 capable compilers and the scons build tool to
build along with set of Python libraries for supporting the building and
configuration of the following third party libraries upon which CPPR depends
- Mercury (https://github.com/mercury-hpc/mercury)
- CCI (wget http://cci-forum.com/wp-content/uploads/2015/12/cci-0.3.0.tar.gz)
  for RPC and underneath communication.
- Mercury uses openpa (http://git.mcs.anl.gov/radix/openpa.git) for atomic
  operation.
- MCL and PMIx (https://github.com/pmix/master) for collective communication
  The PMIx uses hwloc library (wget https://www.open-mpi.org/software/hwloc/v1.11/downloads/hwloc-1.11.2.tar.gz).
- Openmpi runtime environment
  The ompi needs to be compiled with the external PMIx/hwloc used by MCL and
  PMIx (an example configuration is "./configure
  --with-pmix=/your_pmix_install_path /
  --with-hwloc=/your_hwloc_install_path --with-libevent=external").
- To run unit tests, the CUnit-devel package is required

The build tools distributed as a submodule to IOF can be used to build these
prerequisites for you.

After checkout, or pull, update the submodules

git submodule init; git submodule update

To build with all dependent modules installed

scons; scons install

To build the dependencies automatically

scons --build-deps=yes; scons install

It is recommended, though not required, to use scons 2.4 or later.

To get more options help messages, type
scons -h
