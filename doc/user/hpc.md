# HPC I/O Middleware Support

Several HPC I/O middleware libraries have been ported to the native DAOS API.

## MPI-IO

DAOS has its own MPI-IO ROMIO ADIO driver, located in a MPICH fork on
[GitHub](https://github.com/daos-stack/mpich). 
This driver has been merged in the upstream MPICH repo.

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

## HDF5

A [HDF5 DAOS connector](https://bitbucket.hdfgroup.org/projects/HDF5VOL/repos/daos-vol/browse)
is available from the HDF Group. Please refer to the [HDF5 DAOS VOL Connector Users Guide](https://bitbucket.hdfgroup.org/projects/HDF5VOL/repos/daos-vol/browse/docs/users_guide.pdf)
for instructions on how to build and use HDF5 with DAOS.

