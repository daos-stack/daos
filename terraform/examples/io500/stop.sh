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


set -eo pipefail
trap 'echo "Hit an unexpected and unchecked error. Exiting."' ERR

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" &>/dev/null && pwd -P)

# shellcheck source=_log.sh
source "${SCRIPT_DIR}/_log.sh"

# Directory where all generated files were stored
IO500_TMP="${SCRIPT_DIR}/tmp"

# Directory containing config files
CONFIG_DIR="${CONFIG_DIR:-${SCRIPT_DIR}/config}"

# Config file in ./config that is used to spin up the environment and configure IO500
CONFIG_FILE="${CONFIG_FILE:-config.sh}"

# active_config.sh is a symlink to the last config file used by start.sh
# shellcheck source=/dev/null
ACTIVE_CONFIG="${CONFIG_DIR}/active_config.sh"

# Source the active config file that was last used by ./start.sh
if [[ ! -L "${ACTIVE_CONFIG}" ]]; then
  log.error "'${ACTIVE_CONFIG}' symlink does not exist."
  log.error "Either the start.sh script was never run or it did not run successfully."
  log.error "Unable to perform 'terraform destroy'. Exiting."
  exit 1
fi

# Source the last configuration that was used by the start.sh script
# shellcheck source=/dev/null
source "$(readlink "${ACTIVE_CONFIG}")"

log.section "Destroying DAOS Servers & Clients"

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

if [[ -f "${SCRIPT_DIR}/login" ]]; then
  rm -f "${SCRIPT_DIR}/login"
fi
