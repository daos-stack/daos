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
# Configures the /etc/daos/daos_*.yml files on daos-server and daos-client
# instances.
#
# TODO: Move everything in this script to Terraform and/or startup scripts.
#

SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )"
SCRIPT_COMPLETED_FILE="${SCRIPT_DIR}/${SCRIPT_NAME}.completed"
CONFIG_FILE="${SCRIPT_DIR}/config.sh"

# Source config file to load variables
source "${CONFIG_FILE}"

log() {
  if [[ -t 1 ]]; then tput setaf 14; fi
  printf -- "\n%s\n\n" "${1}"
  if [[ -t 1 ]]; then tput sgr0; fi
}

DAOS_FIRST_SERVER=$(head -n 1 ~/hosts_servers)

check_already_run() {
  # Check to see if this script has already run
  if [[ -f "${SCRIPT_COMPLETED_FILE}" ]]; then
    # This script has already been run and doesn't need to run again
    exit 0
  fi
}

update_ssh_dir() {
  # Clear ~/.ssh/known_hosts so we don't run into any issues
  clush --hostfile=hosts_all --dsh 'rm -f ~/.ssh/known_hosts'

  # Copy ~/.ssh directory to all instances
  pdcp -w^hosts_all -r ~/.ssh ~/
}

configure_servers() {

  echo "Getting /etc/daos/daos_server.yml from ${DAOS_FIRST_SERVER}"

  scp ${DAOS_FIRST_SERVER}:/etc/daos/daos_server.yml "${SCRIPT_DIR}/"

  echo "Updating daos_server.yml"

  # Set nr_hugepages value
  # nr_hugepages = (targets * 1Gib) / hugepagesize
  #    Example: for 8 targets and Hugepagesize = 2048 kB:
  #       Targets = 8
  #       1Gib = 1048576 KiB
  #       Hugepagesize = 2048kB
  #       nr_hugepages=(8*1048576) / 2048
  #       So nr_hugepages value is 4096
  hugepagesize=$(ssh ${DAOS_FIRST_SERVER} "grep Hugepagesize /proc/meminfo | awk '{print \$2}'")
  nr_hugepages=$(( (${DAOS_SERVER_DISK_COUNT}*1048576) / ${hugepagesize} ))
  sed -i "s/^nr_hugepages:.*/nr_hugepages: ${nr_hugepages}/g" "${SCRIPT_DIR}/daos_server.yml"
  sed -i "s/^crt_timeout:.*/crt_timeout: ${DAOS_SERVER_CRT_TIMEOUT}/g" "${SCRIPT_DIR}/daos_server.yml"
  sed -i "s/^\(\s*\)targets:.*/\1targets: ${DAOS_SERVER_DISK_COUNT}/g" "${SCRIPT_DIR}/daos_server.yml"
  sed -i "s/^\(\s*\)scm_size:.*/\1scm_size: ${DAOS_SERVER_SCM_SIZE}/g" "${SCRIPT_DIR}/daos_server.yml"

  # Copy daos_server.yml to all servers
  echo "Stopping daos_server on DAOS servers"
  clush --hostfile=hosts_servers --dsh "sudo systemctl stop daos_server"
  echo "Copying daos_server.yml to /etc/daos/daos_server.yml on DAOS servers"
  clush --hostfile=hosts_servers --dsh --copy "${SCRIPT_DIR}/daos_server.yml" --dest 'daos_server.yml'
  clush --hostfile=hosts_servers --dsh 'sudo cp -f daos_server.yml /etc/daos/'
  echo "Starting daos_server on on DAOS servers"
  clush --hostfile=hosts_servers --dsh 'sudo systemctl start daos_server'
}

configure_clients() {
  echo "Getting /etc/daos/daos_agent.yml and /etc/daos/daos_control.yml from ${DAOS_FIRST_SERVER}"

  scp ${DAOS_FIRST_SERVER}:/etc/daos/daos_agent.yml "${SCRIPT_DIR}/"
  scp ${DAOS_FIRST_SERVER}:/etc/daos/daos_control.yml "${SCRIPT_DIR}/"

  echo "Stopping daos_agent on DAOS clients"
  clush --hostfile=hosts_clients --dsh "sudo systemctl stop daos_agent"

  echo "Copying ~/daos_agent.yml to /etc/daos/daos_agent.yml on DAOS clients"
  clush --hostfile=hosts_clients --dsh --copy "daos_agent.yml" --dest "daos_agent.yml"
  clush --hostfile=hosts_clients --dsh "sudo cp -f daos_agent.yml /etc/daos/"

  echo "Copying ~/daos_control.yml to /etc/daos/daos_control.yml on DAOS clients"
  clush --hostfile=hosts_clients --dsh --copy "daos_control.yml" --dest "daos_control.yml"
  clush --hostfile=hosts_clients --dsh "sudo cp -f daos_control.yml /etc/daos/"

  echo "Starting daos_agent on DAOS clients"
  clush --hostfile=hosts_clients --dsh "sudo systemctl start daos_agent"
}

create_completed_file() {
  touch "${SCRIPT_COMPLETED_FILE}"
}

main() {
  log "Start configuring DAOS Server and Client instances"
  check_already_run
  update_ssh_dir
  configure_servers
  configure_clients
  create_completed_file
  log "Finished configuring DAOS Server and Client instances"
}

main
