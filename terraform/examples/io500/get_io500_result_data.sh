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

# Each time the run_io500-sc22.sh script is run on the first DAOS client
# it generates a tar.gz file that contains the result files from the run.
# This script will download the tar.gz files for all runs and store them in
# a the terraform/examples/io500/results directory on your local system.
# If the terraform/examples/io500/results directory doesn't exsist, it will be
# created.
#
# After running the run_io500-sc22.sh script on the first DAOS client node, log
# out and run this script before running `terraform destroy`. This will save
# the results locally so that you can view them after the cluster is destroyed.
#

set -eo pipefail
trap 'echo "Hit an unexpected and unchecked error. Exiting."' ERR

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" &>/dev/null && pwd -P)
LOCAL_RESULTS_DIR="${SCRIPT_DIR}/results"
ACTIVE_CONFIG="${SCRIPT_DIR}/config/active_config.sh"

source "${SCRIPT_DIR}/_log.sh"
# shellcheck disable=SC2034
LOG_LEVEL=INFO

load_active_config() {
  if [[ -L ${ACTIVE_CONFIG} ]]; then
    # shellcheck source=/dev/null
    source "$(readlink "${ACTIVE_CONFIG}")"
  else
    log.error "No active config exists in ${ACTIVE_CONFIG}. Exiting..."
    exit 1
  fi
}

get_first_client_ip() {
  FIRST_CLIENT_IP=$(grep ssh "${SCRIPT_DIR}/login" | awk '{print $4}')
  log.debug "FIRST_CLIENT_IP=${FIRST_CLIENT_IP}"
}

download_results_archive() {
  local src="$1"
  log.debug "Downloading ${FIRST_CLIENT_IP}:${src} to ${LOCAL_RESULTS_DIR}/"
  scp -F tmp/ssh_config "${FIRST_CLIENT_IP}":"${src}" "${LOCAL_RESULTS_DIR}/"
}

print_result_value() {
  local summary_file="$1"
  local metric="$2"
  local timestamp="$3"
  local metric_line
  metric_line=$(grep "${metric}" "${summary_file}")
  local metric_value
  metric_value=$(echo "${metric_line}" | awk '{print $3}')
  local metric_measurment
  metric_measurment=$(echo "${metric_line}" | awk '{print $4}')
  local metric_time_secs
  metric_time_secs=$(echo "${metric_line}" | awk '{print $7}')
  printf "%s %s %s %s %s %s\n" "${IO500_TEST_CONFIG_ID}" "${timestamp}" "${metric}" "${metric_value}" "${metric_measurment}" "${metric_time_secs}"
}

print_results() {
  local result_summary_file=$1
  local timestamp=$2

  print_result_value "${result_summary_file}" "ior-easy-write" "${timestamp}"
  print_result_value "${result_summary_file}" "mdtest-easy-write" "${timestamp}"
  print_result_value "${result_summary_file}" "ior-hard-write" "${timestamp}"
  print_result_value "${result_summary_file}" "mdtest-hard-write" "${timestamp}"
  print_result_value "${result_summary_file}" "find" "${timestamp}"
  print_result_value "${result_summary_file}" "ior-easy-read" "${timestamp}"
  print_result_value "${result_summary_file}" "mdtest-easy-stat" "${timestamp}"
  print_result_value "${result_summary_file}" "ior-hard-read" "${timestamp}"
  print_result_value "${result_summary_file}" "mdtest-hard-stat" "${timestamp}"
  print_result_value "${result_summary_file}" "mdtest-easy-delete" "${timestamp}"
  print_result_value "${result_summary_file}" "mdtest-hard-read" "${timestamp}"
  print_result_value "${result_summary_file}" "mdtest-hard-delete" "${timestamp}"

  #print bandwidth line
  local bandwidth_line
  bandwidth_line="$(grep 'SCORE' "${result_summary_file}" | cut -d ':' -f 1 | sed 's/\[SCORE \] //g')"
  printf "%s %s %s\n" "${IO500_TEST_CONFIG_ID}" "${timestamp}" "${bandwidth_line}"

  local iops_line
  iops_line="$(grep 'SCORE' "${result_summary_file}" | sed 's/\[SCORE \] //g' | cut -d ':' -f 2 | awk '{$1=$1;print}')"
  printf "%s %s %s\n" "${IO500_TEST_CONFIG_ID}" "${timestamp}" "${iops_line}"

  local total_line
  total_line="$(grep 'SCORE' "${result_summary_file}" | sed 's/\[SCORE \] //g' | cut -d ':' -f 3 | awk '{$1=$1;print}' | sed 's/ \[INVALID\]//g')"
  printf "%s %s %s\n" "${IO500_TEST_CONFIG_ID}" "${timestamp}" "${total_line}"
}

process_results_file() {
  local results_file="$1"
  local tmp_dir="${LOCAL_RESULTS_DIR}/tmp/${timestamp}"
  local timestamp
  timestamp=$(tar --to-stdout -xzf "${results_file}" io500_run_timestamp.txt)

  log.info "Processing results file: $(basename "${results_file}")" 1
  mkdir -p "${tmp_dir}"
  log.debug "Extracting ${results_file} to ${tmp_dir}"
  tar -xzf "${results_file}" -C "${tmp_dir}"

  local result_summary_file
  result_summary_file=$(find "${tmp_dir}" -type f -name result_summary.txt)

  print_results "${result_summary_file}" "${timestamp}"

  if [[ -d "${LOCAL_RESULTS_DIR}/tmp" ]]; then
    log.debug "Deleting tmp dir: ${LOCAL_RESULTS_DIR}/tmp"
    rm -rf "${LOCAL_RESULTS_DIR}/tmp"
  fi
}

get_results_tar_files() {
  log.info "Getting results files for ${IO500_TEST_CONFIG_ID}"

  if [[ ! -f ./tmp/ssh_config ]]; then
    log.error "Missing ./tmp/ssh_config file. Exiting."
    exit 1
  fi

  log.debug "Getting list of results *.tar.gz files"
  ssh -F ./tmp/ssh_config "${FIRST_CLIENT_IP}" \
    "find \"\$(pwd)\" -type f -name '${IO500_TEST_CONFIG_ID}*.tar.gz'" \
    > ./tmp/results_tar_files_list.txt

  mkdir -p "${LOCAL_RESULTS_DIR}"
  while read -r result_file; do
    if [[ ! -f "${LOCAL_RESULTS_DIR}/${result_file##*/}" ]]; then
      log.info "Downloading results file: ${result_file##*/}"
      log.debug "Downloading ${FIRST_CLIENT_IP}:${result_file} to ${LOCAL_RESULTS_DIR}/"
      scp -q -F tmp/ssh_config "${FIRST_CLIENT_IP}":"${result_file}" "${LOCAL_RESULTS_DIR}/"
      process_results_file "${LOCAL_RESULTS_DIR}/${result_file##*/}"
      printf "\n"
    else
      log.info "File already downloaded: ${result_file##*/}"
    fi
  done < ./tmp/results_tar_files_list.txt
}

main() {
  log.section "Download IO500 Results Archives"
  load_active_config
  get_first_client_ip
  get_results_tar_files
}

main
