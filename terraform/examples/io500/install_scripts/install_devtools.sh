#!/usr/bin/env bash
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
set -eo pipefail
trap 'echo "An unexpected error occurred. Exiting."' ERR

# BEGIN: Logging variables and functions
declare -A LOG_LEVELS=([DEBUG]=0 [INFO]=1  [WARN]=2   [ERROR]=3 [FATAL]=4 [OFF]=5)
declare -A LOG_COLORS=([DEBUG]=2 [INFO]=12 [WARN]=3 [ERROR]=1 [FATAL]=9 [OFF]=0 [OTHER]=15)
LOG_LEVEL=INFO

log() {
  local msg="$1"
  local lvl=${2:-INFO}
  if [[ ${LOG_LEVELS[$LOG_LEVEL]} -le ${LOG_LEVELS[$lvl]} ]]; then
    if [[ -t 1 ]]; then tput setaf "${LOG_COLORS[$lvl]}"; fi
    printf "[%-5s] %s\n" "$lvl" "${msg}" 1>&2
    if [[ -t 1 ]]; then tput sgr0; fi
  fi
}

log.debug() { log "${1}" "DEBUG" ; }
log.info()  { log "${1}" "INFO"  ; }
log.warn()  { log "${1}" "WARN"  ; }
log.error() { log "${1}" "ERROR" ; }
log.fatal() { log "${1}" "FATAL" ; }
# END: Logging variables and functions

install_pkgs_centos7() {
  log.info "Installing Development Tools"
  yum group install -y "Development Tools"

  log.info "Installing additional packages"
  yum install -y bzip2-devel clustershell git jq \
    libarchive-devel libuuid-devel openssl-devel patch rsync wget
}

install_pkgs_centos8() {
  dnf install -y dnf-plugins-core
  dnf config-manager --set-enabled powertools
  log.info "Installing Development Tools"
  dnf group install -y "Development Tools"

  log.info "Installing additional packages"
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
      install_pkgs_centos7
      ;;
    almalinux_8|centos_8|rhel_8|rocky_8)
      install_pkgs_centos8
      ;;
    *)
      log_error "ERROR: Unsupported OS: ${OS_VERSION_ID}. Exiting."
      exit 1
      ;;
  esac
}

main() {
  log.info "Installing Development Tools and misc packages"
  install_pkgs
}

main
