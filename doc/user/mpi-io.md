# MPI-IO Support

The Message Passing Interface (MPI) Standard, 
maintained by the [MPI Forum](https://www.mpi-forum.org/docs/),
includes a chapter on MPI-IO. 

[ROMIO](https://www.mcs.anl.gov/projects/romio/) is a well-known
implementation of MPI-IO and is included in many MPI implementations.

DAOS provides its own MPI-IO ROMIO ADIO driver, located in a MPICH fork on
the [DAOS-Stack GitHub](https://github.com/daos-stack/mpich). 
This driver has been merged in the upstream MPICH repository.


## MPI Implementations that support DAOS

### MPICH

The DAOS ROMIO ADIO driver has been accepted into [MPICH](https://www.mpich.org/).
It is included in [mpich-3.4b1 (alpha release)](https://www.mpich.org/downloads/),
but not in mpich-3.3.2 (stable release).

### Intel MPI

The [Intel MPI Library](https://software.intel.com/content/www/us/en/develop/tools/mpi-library.html) 
includes DAOS support since the 
[2019.8 release](https://software.intel.com/content/www/us/en/develop/articles/intel-mpi-library-release-notes-linux.html).

Note that Intel MPI uses `libfabric` (both 2019.8 and 2019.9 use 
`libfabric-1.10.1-impi`).  Care must be taken to ensure that the installed 
version of libfabric works cleanly with both DAOS and Intel MPI
(DAOS 1.0.1 includes `libfabric-1.9.0`, and the 
DAOS 1.1.1 pre-release includes `libfabric-1.11.1`).

### Open MPI

[Open MPI](https://www.open-mpi.org/) 4.0.5 does not yet provide DAOS support.
Since one of its MPI-IO implementations is based on ROMIO, 
it will likely pick up DAOS support in an upcoming release.

### MVAPICH2

[MVAPICH2](https://mvapich.cse.ohio-state.edu/) 2.3.4 does not yet provide DAOS support.
Since its MPI-IO implementation is based on ROMIO, 
it will likely pick up DAOS support in an upcoming release.


## Building MPI-IO with DAOS Support

To build the MPI-IO driver with DAOS support:

```bash
export MPI_LIB=""

git clone https://github.com/daos-stack/mpich

cd mpich

./autogen.sh

mkdir build; cd build

../configure --prefix=dir --enable-fortran=all --enable-romio \
  --enable-cxx --enable-g=all --enable-debuginfo --with-device=ch3:sock \
  --with-file-system=ufs+daos --with-daos=dir --with-cart=dir

make -j8; make install
```

Set the `PATH` and `LD_LIBRARY_PATH` to where you want to build your client
apps or libs that use MPI to the installed MPICH.

Build any client (HDF5, ior, mpi test suites) normally with the mpicc command
and mpich library installed above (see child pages).

To run an example:

1. Create a DAOS pool on the DAOS server(s).
   This will return a pool uuid "puuid" and service rank list "svcl".
2. Create a POSIX type container:
   `daos cont create --pool=puuid --svc=svcl --type=POSIX`
   This will return a container uuid "cuuid".
3. At the client side, the following environment variables need to be set:
   `export DAOS_POOL=puuid; export DAOS_SVCL=svcl; export DAOS_CONT=cuuid`.
   Alternatively, the unified namespace mode can be used instead.
3. Run the client application or test.

Limitations to the current implementation include:

-   Reading Holes does not return 0, but leaves the buffer untouched.

-   No support for MPI file atomicity, preallocate, or shared file pointers.

