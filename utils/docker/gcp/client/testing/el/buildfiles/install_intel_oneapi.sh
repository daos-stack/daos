#!/usr/bin/env bash

set -eo pipefail
trap 'echo "An unexpected error occurred. Exiting."' ERR

# Install Intel MPI from Intel oneAPI package
# Disabling gpg check until Intel releases an updated pub key
cat > /etc/yum.repos.d/oneAPI.repo <<EOF
[oneAPI]
name=Intel(R) oneAPI repository
baseurl=https://yum.repos.intel.com/oneapi
enabled=1
gpgcheck=0
repo_gpgcheck=0
gpgkey=https://yum.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS-2023.PUB
EOF

export MPI_VERSION="2021.5.1"
export MPI_PATH="/opt/intel/oneapi/mpi/$MPI_VERSION/bin"
# Import GPG Key
rpm --import https://yum.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS-2023.PUB

# Install Intel OneAPI MPI
yum install -y intel-oneapi-mpi-$MPI_VERSION intel-oneapi-mpi-devel-$MPI_VERSION

echo "export PATH=$MPI_PATH:$PATH" >> ~/.bashrc
