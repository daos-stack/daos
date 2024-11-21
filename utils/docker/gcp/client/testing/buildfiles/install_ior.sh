#!/bin/bash
#
# Install IOR
#
# Instructions that were referenced to create this script are at
# https://daosio.atlassian.net/wiki/spaces/DC/pages/11167301633/IO-500+SC22
#
#

set -eo pipefail
trap 'echo "An unexpected error occurred. Exiting."' ERR

export IOR_COMMIT="acd3a15"

# The following variable names match the instructions at
# https://daosio.atlassian.net/wiki/spaces/DC/pages/11167301633/IO-500+SC22
export MY_DAOS_INSTALL_PATH="/usr"
MY_IOR_PATH="${MY_IOR_PATH:-/opt/ior-${IOR_COMMIT}}"

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

log_debug() { log "${1}" "DEBUG" ; }
log_info()  { log "${1}" "INFO"  ; }
log_warn()  { log "${1}" "WARN"  ; }
log_error() { log "${1}" "ERROR" ; }
log_fatal() { log "${1}" "FATAL" ; }
# END: Logging variables and functions

clone_ior_repo() {
  log_info "Cloning repo: https://github.com/hpc/ior.git,${IOR_COMMIT}"

  git clone https://github.com/hpc/ior.git "${MY_IOR_PATH}"
}

install_ior() {
  pushd "${MY_IOR_PATH}"
  git checkout ${IOR_COMMIT}
  ./bootstrap
  ./configure MPICC=/usr/lib64/mpich/bin/mpicc --with-daos="${MY_DAOS_INSTALL_PATH}"
  make
  make install
  popd
}

add_to_path() {
  echo "export PATH=${MY_IOR_PATH}:$PATH" >> ~/.bashrc
}

main() {
  clone_ior_repo
  install_ior
  add_to_path
}

main "$@"

