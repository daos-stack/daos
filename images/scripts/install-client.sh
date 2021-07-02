#!/bin/bash
# Copyright 2021 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


echo "Installing DAOS version ${DAOS_VERSION}"

# Install 1.1.4 from non-official site for now
tee /etc/yum.repos.d/daos.repo > /dev/null <<EOF
[daos]
name=daos (packages.daos.io)
baseurl=http://packages.daos.io/private/1c648136-2100-486b-83b4-002222a45bee/v1.1.4/CentOS7/
enabled=1
gpgcheck=0
repo_gpgcheck=0
EOF

# Install 1.2.0 RPMs from official site
tee /etc/yum.repos.d/daos.repo > /dev/null <<EOF
[daos-packages]
name=DAOS v1.2 Packages
baseurl=https://packages.daos.io/v1.2/CentOS7/packages/x86_64/
enabled=1
gpgcheck=1
protect=1
gpgkey=https://packages.daos.io/RPM-GPG-KEY
EOF

# Install DAOS RPMs
yum install -y daos-client daos-devel

# enable daos_server in systemd (will be started automatically at boot time)
systemctl enable daos_agent

echo "Installing Intel oneAPI MPI"

# Install Intel MPI from oneAPI package
tee > /etc/yum.repos.d/oneAPI.repo << EOF
[oneAPI]
name=Intel(R) oneAPI repository
baseurl=https://yum.repos.intel.com/oneapi
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=https://yum.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB
EOF

yum install -y intel-oneapi-mpi intel-oneapi-mpi-devel

# Install some other software helpful for development
# (e.g. to compile ior or fio)
yum install -y gcc git autoconf automake libuuid-devel devtoolset-9-gcc

# TODO:
# - enable gvnic
