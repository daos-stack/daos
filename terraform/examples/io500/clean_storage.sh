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
# Clean storage on DAOS servers
#

set -eo pipefail
trap 'echo "Hit an unexpected and unchecked error. Exiting."' ERR

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" &>/dev/null && pwd -P)
CONFIG_FILE="${SCRIPT_DIR}/config.sh"
SERVER_HOSTS_FILE="${SCRIPT_DIR}/hosts_servers"

# shellcheck source=_log.sh
source "${SCRIPT_DIR}/_log.sh"

# Source config file to load variables
# shellcheck source=/dev/null
source "${CONFIG_FILE}"

log.info "Start cleaning storage on DAOS servers"

clush --hostfile="${SERVER_HOSTS_FILE}" --dsh 'sudo systemctl stop daos_server'
clush --hostfile="${SERVER_HOSTS_FILE}" --dsh 'sudo rm -rf /var/daos/ram/*'
clush --hostfile="${SERVER_HOSTS_FILE}" --dsh '[[ -d /var/daos/ram ]] && sudo umount /var/daos/ram/ || echo "/var/daos/ram/ unmounted"'
clush --hostfile="${SERVER_HOSTS_FILE}" --dsh 'sudo systemctl start daos_server'

# shellcheck disable=SC2016
clush --hostfile="${SERVER_HOSTS_FILE}" --dsh 'while /bin/netstat -an | /bin/grep \:10001 | /bin/grep -q LISTEN; [ $? -ne 0 ]; do let TRIES-=1; if [ $TRIES -gt 1 ]; then echo "waiting ${TRIES}"; sleep $WAIT;else echo "Timed out";break; fi;done'

log.info "Finished cleaning storage on DAOS servers"
