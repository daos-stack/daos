# HDF5 Support

The Hierarchical Data Format Version 5 (HDF5) specification and tools are maintained by the HDF
Group (https://www.hdfgroup.org/).  Applications that use HDF5 can utilize DAOS in two ways:

## HDF5 over MPI-IO

Parallel HDF5 is typically layered on top of MPI-IO.  By building HDF5 and the user application with
an MPI stack that includes the DAOS support for MPI-IO, such HDF5 applications can be run on top of
DAOS.

See the [MPI-IO section](mpi-io.md) for instructions on how to build and run applications with
MPI-IO DAOS support.

## HDF5 DAOS VOL Connector

A [HDF5 DAOS connector](https://github.com/HDFGroup/vol-daos) is available from the HDF
Group. Currently the DAOS connector requires HDF5 version 1.13.1. The DAOS connector version to be
used should be v1.1.0.

Please refer to the [HDF5 DAOS VOL Connector Users
Guide](https://github.com/HDFGroup/vol-daos/blob/master/docs/users_guide.pdf) for instructions on
how to build and use HDF5 with this DAOS VOL connector.  A brief set of build instructions are also
published on the [DAOS User Community wiki
page](https://daosio.atlassian.net/wiki/spaces/DC/pages/11138695214/HDF5+VOL+Connector).

