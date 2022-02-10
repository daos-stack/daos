#!/bin/bash
#
# Clean storage on DAOS servers
#

#set -e
#trap 'echo "Hit an unexpected and unchecked error. Exiting."' ERR

SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )"
CONFIG_FILE="${SCRIPT_DIR}/config.sh"
SERVER_HOSTS_FILE="${SCRIPT_DIR}/hosts_servers"

# Source config file to load variables
source "${CONFIG_FILE}"

log() {
  if [[ -t 1 ]]; then tput setaf 14; fi
  printf -- "\n%s\n\n" "${1}"
  if [[ -t 1 ]]; then tput sgr0; fi
}

log "Start cleaning storage on DAOS servers"

clush --hostfile="${SERVER_HOSTS_FILE}" --dsh 'sudo systemctl stop daos_server'
clush --hostfile="${SERVER_HOSTS_FILE}" --dsh 'sudo rm -rf /var/daos/ram/*'
clush --hostfile="${SERVER_HOSTS_FILE}" --dsh '[[ -d /var/daos/ram ]] && sudo umount /var/daos/ram/ || echo "/var/daos/ram/ unmounted"'
clush --hostfile="${SERVER_HOSTS_FILE}" --dsh 'sudo systemctl start daos_server'

log "Finished cleaning storage on DAOS servers"

