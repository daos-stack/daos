#!/usr/bin/env bash

set -eo pipefail
trap 'echo "An unexpected error occurred. Exiting."' ERR

# download the key to system keyring
wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB \
| gpg --dearmor | sudo tee /usr/share/keyrings/oneapi-archive-keyring.gpg > /dev/null

# add signed entry to apt sources and configure the APT client to use Intel repository:
echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] https://apt.repos.intel.com/oneapi all main" | sudo tee /etc/apt/sources.list.d/oneAPI.list

sudo apt update

export MPI_VERSION="2021.5.1"
export MPI_PATH="/opt/intel/oneapi/mpi/$MPI_VERSION/bin"

sudo apt install -y intel-oneapi-mpi-$MPI_VERSION intel-oneapi-mpi-devel-$MPI_VERSION

echo "export PATH=$MPI_PATH:$PATH" >> ~/.bashrc
