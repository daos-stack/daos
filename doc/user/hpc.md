# HPC I/O Middleware Support

Several HPC I/O middleware libraries have been ported to the native API.

## MPI-IO

DAOS has its own MPI-IO ROMIO ADIO driver located in a MPICH fork on
[GitHub](https://github.com/daos-stack/mpich). This driver has been merged in
the upstream MPICH repo.

To build the MPI-IO driver:

-   export MPI_LIB=""

-   download the mpich repo from above

-   ./autogen.sh

-   mkdir build; cd build

-   ../configure --prefix=dir --enable-fortran=all --enable-romio
    --enable-cxx --enable-g=all --enable-debuginfo --with-device=ch3:sock
    --with-file-system=ufs+daos --with-daos=dir --with-cart=dir

-   make -j8; make install

Switch the `PATH` and `LD_LIBRARY_PATH` to where you want to build your client
apps or libs that use MPI to the installed MPICH.

Build any client (HDF5, ior, mpi test suites) normally with the mpicc and mpich
library installed above (see child pages).

To run an example:

1. Launch DAOS server(s) and create a pool.
   This will return a pool uuid "puuid" and service rank list "svcl".
2. Create a POSIX type container:
   daos cont create --pool=puuid --svc=svcl --type=POSIX
   This will return a container uuid "cuuid".
3. At the client side, the following environment variables need to be set:
   `export DAOS_POOL=puuid; export DAOS_SVCL=svcl; export DAOS_CONT=cuuid`
   Alternatively, the unified namespace mode can be used instead.
3. Run the client application or test.

Limitations to the current implementation include:

-   Reading Holes does not return 0, but leaves the buffer untouched.

-   No support for MPI file atomicity, preallocate, shared file pointers.

## HDF5

A [HDF5 DAOS connector](https://bitbucket.hdfgroup.org/projects/HDF5VOL/repos/daos-vol/browse)
is available. Please refer to the DAOS VOL connector user guide[^3] for
instructions on how to build and use it.

[^3]: https://bitbucket.hdfgroup.org/projects/HDF5VOL/repos/daos-vol/browse/docs/users_guide.pdf
