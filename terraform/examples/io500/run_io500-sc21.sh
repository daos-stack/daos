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
# Cleans DAOS storage and runs an IO500 benchmark
#
# Instructions that were referenced to create this script are at
# https://daosio.atlassian.net/wiki/spaces/DC/pages/11055792129/IO-500+SC21
#

IO500_VERSION_TAG=io500-sc21

# # Determine if this script is being sourced
# # See https://stackoverflow.com/questions/2683279/how-to-detect-if-a-script-is-being-sourced
# (return 0 2>/dev/null) && sourced=1 || sourced=0

set -e
trap 'echo "Hit an unexpected and unchecked error. Unmounting and exiting."; unmount' ERR

SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )"
CONFIG_FILE="${SCRIPT_DIR}/config.sh"

# Comma separated list of servers needed for the dmg command
# TODO: Figure out a better way for this script to get the list of servers
#       Requiring the hosts_servers file is not ideal
SERVER_LIST=$(awk -vORS=, '{ print $1 }' "${SCRIPT_DIR}/hosts_servers" | sed 's/,$/\n/')

# Source config file to load variables
if [[ -f "${CONFIG_FILE}" ]]; then
  source "${CONFIG_FILE}"
fi

# Set environment variable defaults if not already set
# This allows for the variables to be set to different values externally.
IO500_STONEWALL_TIME="${IO500_STONEWALL_TIME:-5}"
IO500_INSTALL_DIR="${IO500_INSTALL_DIR:-/usr/local}"
IO500_DIR="${IO500_DIR:-${IO500_INSTALL_DIR}/${IO500_VERSION_TAG}}"
IO500_RESULTS_DFUSE_DIR="${IO500_RESULTS_DFUSE_DIR:-${HOME}/daos_fuse/${IO500_VERSION_TAG}/results}"
IO500_RESULTS_DIR="${IO500_RESULTS_DIR:-${HOME}/${IO500_VERSION_TAG}/results}"
DAOS_POOL_LABEL="${DAOS_POOL_LABEL:-io500_pool}"
DAOS_CONT_LABEL="${DAOS_CONT_LABEL:-io500_cont}"

log() {
  local msg
  local print_lines
  msg="$1"
  print_lines="$2"
  # shellcheck disable=SC2155,SC2183
  local line=$(printf "%80s" | tr " " "-")
  if [[ -t 1 ]]; then tput setaf 14; fi
  if [[ "${print_lines}" == 1 ]]; then
    printf -- "\n%s\n %-78s \n%s\n" "${line}" "${msg}" "${line}"
  else
    printf -- "\n%s\n\n" "${msg}"
  fi
  if [[ -t 1 ]]; then tput sgr0; fi
}

log_error() {
  # shellcheck disable=SC2155,SC2183
  if [[ -t 1 ]]; then tput setaf 160; fi
  printf -- "\n%s\n\n" "${1}" >&2;
  if [[ -t 1 ]]; then tput sgr0; fi
}

log_section() {
  log "$1" "1"
}

unmount_defuse() {
  if [[ -d "${IO500_RESULTS_DFUSE_DIR}" ]]; then
    log_section "Unmount DFuse mountpoint ${IO500_RESULTS_DFUSE_DIR}"

    clush --hostfile=hosts_clients --dsh \
      "sudo fusermount3 -u '${IO500_RESULTS_DFUSE_DIR}'"

    clush --hostfile=hosts_clients --dsh \
      "rm -r '${IO500_RESULTS_DFUSE_DIR}'"

    clush --hostfile=hosts_clients --dsh \
      "mount | sort | grep dfuse || true"

    log "fusermount3 complete!"
  fi
}

cleanup(){
  log_section "Clean up DAOS storage"
  unmount_defuse
  "${SCRIPT_DIR}/clean_storage.sh"
}

storage_scan() {
  log "Run DAOS storage scan"
  dmg -l ${SERVER_LIST} storage scan --verbose
}

format_storage() {

  log_section "Format DAOS storage"
  dmg -l ${SERVER_LIST} storage format --reformat

  printf "%s" "Waiting for DAOS storage format to finish"
  while true
  do
    if [[ $(dmg -j system query -v | grep joined | wc -l) -eq ${DAOS_SERVER_INSTANCE_COUNT} ]]; then
      printf "\n%s\n" "DAOS storage format finished"
      dmg system query -v
      break
    fi
    printf "%s" "."
    sleep 5
  done

  dmg storage query usage

}

create_pool() {
  log_section "Create pool: label=${DAOS_POOL_LABEL} size=${DAOS_POOL_SIZE}"

  # TODO: Don't hardcode tier-ratio to 3 (-t 3)
  dmg pool create -z ${DAOS_POOL_SIZE} -t 3 -u ${USER} --label=${DAOS_POOL_LABEL}

  echo "Set pool property: reclaim=disabled"
  dmg pool set-prop ${DAOS_POOL_LABEL} --name=reclaim --value=disabled

  echo "Pool created successfully"
  dmg pool query "${DAOS_POOL_LABEL}"

  log "Create container: label=${DAOS_CONT_LABEL}"
  daos container create --type=POSIX --properties="${DAOS_CONT_REPLICATION_FACTOR}" --label="${DAOS_CONT_LABEL}" "${DAOS_POOL_LABEL}"

  #  Show container properties
  daos cont get-prop ${DAOS_POOL_LABEL} ${DAOS_CONT_LABEL}
}

mount_dfuse() {
  if [[ -d "${IO500_RESULTS_DFUSE_DIR}" ]]; then
    log_error "DFuse dir ${IO500_RESULTS_DFUSE_DIR} already exists."
  else
    log_section "Use dfuse to mount ${DAOS_CONT_LABEL} on ${IO500_RESULTS_DFUSE_DIR}"

    clush --hostfile=hosts_clients --dsh \
      "mkdir -p '${IO500_RESULTS_DFUSE_DIR}'"

    clush --hostfile=hosts_clients --dsh \
      "dfuse -S --pool='${DAOS_POOL_LABEL}' --container='${DAOS_CONT_LABEL}' --mountpoint='${IO500_RESULTS_DFUSE_DIR}'"

    sleep 10

    echo "DFuse mount complete!"
  fi
}

io500_prepare() {
  log_section "Load Intel MPI"
  export I_MPI_OFI_LIBRARY_INTERNAL=0
  export I_MPI_OFI_PROVIDER="tcp;ofi_rxm"
  source /opt/intel/oneapi/setvars.sh

  export PATH=$PATH:${IO500_DIR}/bin
  export LD_LIBRARY_PATH=/usr/local/mpifileutils/install/lib64/:$LD_LIBRARY_PATH

  log "Prepare config file 'temp.ini' for IO500"

  # Set the following vars in order to do envsubst with config-full-sc21.ini
  export DAOS_POOL="${DAOS_POOL_LABEL}"
  export DAOS_CONT="${DAOS_CONT_LABEL}"
  export MFU_POSIX_TS=1
  export IO500_NP=$(( ${DAOS_CLIENT_INSTANCE_COUNT} * $(nproc --all) ))

  cp -f "${IO500_DIR}/config-full-sc21.ini" .
  envsubst < config-full-sc21.ini > temp.ini
  sed -i "s|^resultdir.*|resultdir = ${IO500_RESULTS_DFUSE_DIR}|g" temp.ini
  sed -i "s/^stonewall-time.*/stonewall-time = ${IO500_STONEWALL_TIME}/g" temp.ini
  sed -i "s/^transferSize.*/transferSize = 4m/g" temp.ini
  #sed -i "s/^blockSize.*/blockSize = 1000000m/g" temp.ini # This causes failures
  sed -i "s/^filePerProc.*/filePerProc = TRUE /g" temp.ini
  sed -i "s/^nproc.*/nproc = ${IO500_NP}/g" temp.ini

  # Prepare final results directory for the current run
  TIMESTAMP=$(date "+%Y-%m-%d_%H%M%S")
  IO500_RESULTS_DIR_TIMESTAMPED="${IO500_RESULTS_DIR}/${TIMESTAMP}"
  log "Creating directory for results ${IO500_RESULTS_DIR_TIMESTAMPED}"
  mkdir -p "${IO500_RESULTS_DIR_TIMESTAMPED}"
}

run_io500() {
  log_section "Run IO500"
  mpirun -np ${IO500_NP} \
    --hostfile "${SCRIPT_DIR}/hosts_clients" \
    --bind-to socket "${IO500_DIR}/io500" temp.ini
  log "IO500 run complete!"
}

show_pool_state() {
  log "Query pool state"
  dmg pool query "${DAOS_POOL_LABEL}"
}

process_results() {
  log_section "Copy results from ${IO500_RESULTS_DFUSE_DIR} to ${IO500_RESULTS_DIR_TIMESTAMPED}"

  # Copy results from dfuse mount to another directory so we don't lose them
  # when the dfuse mount is removed
  rsync -avh "${IO500_RESULTS_DFUSE_DIR}/" "${IO500_RESULTS_DIR_TIMESTAMPED}/"
  cp temp.ini "${IO500_RESULTS_DIR_TIMESTAMPED}/"

  # Save a copy of the environment variables for the IO500 run
  printenv | sort > "${IO500_RESULTS_DIR_TIMESTAMPED}/env.sh"

  # Save output from "dmg pool query"
  dmg pool query "${DAOS_POOL_LABEL}" > \
    "${IO500_RESULTS_DIR_TIMESTAMPED}/dmg_pool_query_${DAOS_POOL_LABEL}.txt"

  FIRST_SERVER=$(echo ${SERVER_LIST} | cut -d, -f1)
  ssh ${FIRST_SERVER} 'daos_server version' > \
    "${IO500_RESULTS_DIR_TIMESTAMPED}/daos_server_version.txt"

  log "Results files located in ${IO500_RESULTS_DIR_TIMESTAMPED}"
}

main(){
  cleanup
  storage_scan
  format_storage
  create_pool
  mount_dfuse
  io500_prepare
  run_io500
  process_results
  unmount_defuse
}

main
