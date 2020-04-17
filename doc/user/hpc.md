# HPC I/O Middleware Support

Several HPC I/O middleware libraries have been ported to the native API.

## MPI-IO

DAOS has its own MPI-IO ROM ADIO driver located in a MPICH fork on
[GitHub](https://github.com/daos-stack/mpich).

This driver has been submitted upstream for integration.

To build the MPI-IO driver:

-   export MPI_LIB=""

-   download the mpich repo from above and switch to daos_adio branch

-   ./autogen.sh

-   mkdir build; cd build

-   ../configure --prefix=dir --enable-fortran=all --enable-romio
    --enable-cxx --enable-g=all --enable-debuginfo
    --with-file-system=ufs+daos --with-daos=dir --with-cart=dir

-   make -j8; make install

Switch the `PATH` and `LD_LIBRARY_PATH` to where you want to build your client
apps or libs that use MPI to the installed MPICH.

Build any client (HDF5, ior, mpi test suites) normally with the mpicc and mpich
library installed above (see child pages).

To run an example:

1. Launch DAOS server(s) and create a pool as specified in the previous section.
   This will return a pool uuid "puuid" and service rank list "svcl"
2.   At the client side, the following environment variables need to be set:

```bash
        export PATH=/path/to/mpich/install/bin:$PATH
        export LD_LIBRARY_PATH=/path/to/mpich/install/lib:$LD_LIBRARY_PATH
        export MPI_LIB=""
```
2.  `export DAOS_POOL=puuid; export DAOS_SVCL=svcl`
    This is just temporary till we have a better way of passing pool
    connect info to MPI-IO and other middleware over DAOS.
3.  Run the client application or test.

Limitations to the current implementation include:

-   Incorrect MPI_File_set_size and MPI_File_get_size - This will be fixed in
    the future when DAOS correctly supports records enumeration after punch or
    key query for max/min key and recx.

-   Reading Holes does not return 0, but leaves the buffer untouched

-   No support for MPI file atomicity, preallocate, shared file pointers.

## HDF5

A [HDF5 DAOS connector](https://bitbucket.hdfgroup.org/projects/HDF5VOL/repos/daos-vol/browse)
is available. Please refer to the DAOS VOL connector user guide[^3] for
instructions on how to build and use it.

[^3]: https://bitbucket.hdfgroup.org/projects/HDF5VOL/repos/daos-vol/browse/docs/users_guide.pdf
