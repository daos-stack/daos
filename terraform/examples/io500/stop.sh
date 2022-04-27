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


set -e
trap 'echo "Hit an unexpected and unchecked error. Exiting."' ERR

SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )"

# Directory where all generated files were stored
IO500_TMP="${SCRIPT_DIR}/tmp"

# Directory containing config files
CONFIG_DIR="${CONFIG_DIR:-${SCRIPT_DIR}/config}"

# Config file in ./config that is used to spin up the environment and configure IO500
CONFIG_FILE="${CONFIG_FILE:-config.sh}"

# active_config.sh is a symlink to the last config file used by start.sh
ACTIVE_CONFIG="${CONFIG_DIR}/active_config.sh"

log() {
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

# Source the active config file that was last used by ./start.sh
if [[ ! -L "${ACTIVE_CONFIG}" ]]; then
  log_error "'${ACTIVE_CONFIG}' symlink does not exist."
  log_error "Either the start.sh script was never run or it did not run successfully."
  log_error "Unable to perform a terraform destroy. Exiting."
  exit 1
fi

# Source the last configuration that was used by the start.sh script
source "$(readlink ${ACTIVE_CONFIG})"

log_section "Destroying DAOS Servers & Clients"

pushd "${SCRIPT_DIR}/../daos_cluster"
terraform destroy -auto-approve
popd

# Remove the symlink to the last configuration
if [[ -L "${ACTIVE_CONFIG}" ]]; then
  rm -f "${ACTIVE_CONFIG}"
fi

# Clean up the ./tmp directory
if [[ -d "${IO500_TMP}" ]]; then
  rm -r "${IO500_TMP}"
fi
