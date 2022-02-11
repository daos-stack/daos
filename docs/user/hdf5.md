# HDF5 Support

The Hierarchical Data Format Version 5 (HDF5) specification and tools are 
maintained by the HDF Group (https://www.hdfgroup.org/).
Applications that use HDF5 can utilize DAOS in two ways:

## HDF5 over MPI-IO

Parallel HDF5 is typically layered on top of MPI-IO. 
By building HDF5 and the user application with an MPI stack that 
includes the DAOS support for MPI-IO, such HDF5 applications can
be run on top of DAOS. 

See the [MPI-IO section](mpi-io.md) for instructions on how
to build and run applications with MPI-IO DAOS support.

## HDF5 DAOS VOL Connector

A [HDF5 DAOS connector](https://github.com/HDFGroup/vol-daos)
is available from the HDF Group.

Please refer to the [HDF5 DAOS VOL Connector Users 
Guide](https://github.com/HDFGroup/vol-daos/blob/master/docs/users_guide.pdf)
for instructions on how to build and use HDF5 with this DAOS VOL connector.

The presentation [Advancing HDF5’s Parallel I/O for Exascale with 
DAOS](https://www.hdfgroup.org/wp-content/uploads/2020/10/HDF5_HUG_2020_DAOS.pdf)
from the HDF Users Group 2020 describes the HDF5 DAOS VOL Connector Project 
and its current status.  The [video](https://youtu.be/P_V7y_G4vM0) 
of that presentation is also available online.

