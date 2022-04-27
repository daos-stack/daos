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
# Install development tools and other packages needed for IO500
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

log_error() {
  # shellcheck disable=SC2155,SC2183
  if [[ -t 1 ]]; then tput setaf 160; fi
  printf -- "\n%s\n\n" "${1}" >&2;
  if [[ -t 1 ]]; then tput sgr0; fi
}

wait_for_yum_lock() {
  # Wait if another app is currently holding the yum lock
  loop_count=0
  yum_proc_count=$(ps -ef | grep bash | grep -v "grep" | wc -l)
  while [[ yum_proc_count -gt 0 ]] && [[ ${loop_count} -lt 5 ]]
  do
    printf "%s\n" "Waiting for another process to release yum lock"
    sleep 5;
    yum_proc_count=$(ps -ef | grep bash | grep -v "grep" | wc -l)
    loop_count=$((loop_count+1))
  done
  echo "Good to go!"
}

install_pkgs_centos7() {
  yum group install -y "Development Tools"
  yum install -y bzip2-devel clustershell git jq \
    libarchive-devel libuuid-devel openssl-devel patch rsync wget
}

install_pkgs_centos8() {
  dnf install -y dnf-plugins-core
  dnf config-manager --set-enabled powertools
  dnf group install -y "Development Tools"
  dnf install -y bzip2-devel clustershell \
  gcc-toolset-9-gcc gcc-toolset-9-gcc-c++ git jq \
  libarchive-devel libuuid-devel openssl-devel patch rsync wget
}

install_pkgs()  {
  source /etc/os-release
  OS_VERSION=$(echo "${VERSION_ID}" | cut -d. -f1)
  OS_VERSION_ID="${ID,,}_${OS_VERSION}"
  case ${OS_VERSION_ID} in
    centos_7)
      DAOS_OS_VERSION="CentOS7"
      install_pkgs_centos7
      ;;
    centos_8)
      DAOS_OS_VERSION="CentOS8"
      install_pkgs_centos8
      ;;
    rocky_8)
      DAOS_OS_VERSION="CentOS8"
      install_pkgs_centos8
      ;;
    *)
      log_error "ERROR: Unsupported OS: ${OS_VERSION_ID}. Exiting."
      exit 1
      ;;
  esac
}

main() {
  log "Installing Development Tools and misc packages"
  install_pkgs
}

main
