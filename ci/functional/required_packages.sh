#!/bin/bash

set -eux

distro="$1"
client_ver="$2"

if [[ $distro = ubuntu20* ]]; then
    pkgs="openmpi-bin ndctl fio"
elif [[ $distro = el* ]] || [[ $distro = centos* ]] ||
     [[ $distro = leap* ]]; then
    openmpi="openmpi"
    pyver="3"
    prefix=""

    if [[ $distro = el7* ]] || [[ $distro = centos7* ]]; then
        pyver="36"
        openmpi="openmpi3"
        prefix="--exclude ompi"
    elif [[ $distro = leap15* ]]; then
        openmpi="openmpi3"
    fi

    pkgs="$prefix ndctl                \
          fio patchutils ior           \
          romio-tests                  \
          testmpio                     \
          python$pyver-mpi4py-tests    \
          hdf5-mpich-tests             \
          hdf5-$openmpi-tests          \
          hdf5-vol-daos-$openmpi-tests \
          hdf5-vol-daos-mpich-tests    \
          simul-mpich                  \
          simul-$openmpi               \
          MACSio-mpich                 \
          MACSio-$openmpi              \
          mpifileutils-mpich"
else
    echo "I don't know which packages should be installed for distro" \
         "\"$distro\""
    exit 1
fi

echo "$pkgs"

exit 0
