#!/bin/bash

set -eux

distro="$1"
client_ver="$2"

if [[ $distro = el8* ]] || [[ $distro = centos8* ]]; then
    pkgs="hwloc ndctl fio patchutils"
elif [[ $distro = el7 ]] || [[ $distro = centos7 ]] ||
     [[ $distro = leap* ]]; then
    pkgs="hwloc ndctl fio                         \
          patchutils ior-hpc-daos-${client_ver}   \
          romio-tests-daos-${client_ver}          \
          testmpio                                \
          mpi4py-tests                            \
          hdf5-mpich-tests                        \
          hdf5-openmpi3-tests                     \
          hdf5-vol-daos-mpich-tests               \
          hdf5-vol-daos-openmpi3-tests            \
          MACSio-mpich                            \
          MACSio-openmpi3                         \
          mpifileutils-mpich-daos-${client_ver}"
elif [[ $distro = ubuntu20* ]]; then
    pkgs="openmpi-bin ndctl fio"
else
    echo "I don't know which packages should be installed for distro"
         "\"$distro\""
    exit 1
fi

echo "$pkgs"
exit 0
