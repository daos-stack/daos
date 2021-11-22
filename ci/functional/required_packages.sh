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

    pkgs="$prefix ndctl fio patchutils"
else
    echo "I don't know which packages should be installed for distro"
         "\"$distro\""
    exit 1
fi

echo "$pkgs"

exit 0
