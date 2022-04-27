#!/bin/bash
# Copyright 2022 Intel Corporation
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

#
# Install Intel OneAPI
#

set -e
trap 'echo "An unexpected error occurred. Exiting."' ERR

log() {
  # shellcheck disable=SC2155,SC2183
  local line=$(printf "%80s" | tr " " "-")
  if [[ -t 1 ]]; then tput setaf 14; fi
  printf -- "\n%s\n %-78s \n%s\n" "${line}" "${1}" "${line}"
  if [[ -t 1 ]]; then tput sgr0; fi
}

wait_for_yum_lock() {
  # Wait if another app is currently holding the yum lock
  loop_count=0
  yum_proc_count=$(ps -ef | grep bash | grep -v "grep" | wc -l)
  while [[ ${loop_count} -lt 5 ]] || [[ numProcess -gt 0 ]]
  do
    printf "%s\n" "Waiting for another process to release yum lock"
    sleep 5;
    yum_proc_count=$(ps -ef | grep bash | grep -v "grep" | wc -l)
    loop_count=$((loop_count+1))
  done
}

install() {
  # Install Intel MPI from Intel oneAPI package
  cat > /etc/yum.repos.d/oneAPI.repo <<EOF
[oneAPI]
name=Intel(R) oneAPI repository
baseurl=https://yum.repos.intel.com/oneapi
enabled=1
gpgcheck=1
repo_gpgcheck=0
gpgkey=https://yum.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB
EOF

  # Import GPG Key
  rpm --import https://yum.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB

  # Refresh yum cache and install Intel OneAPI
  yum clean all
  yum makecache
  yum install -y intel-oneapi-mpi intel-oneapi-mpi-devel
}

main() {
  log "Installing Intel oneAPI MPI"
  #wait_for_yum_lock
  install
}

main
