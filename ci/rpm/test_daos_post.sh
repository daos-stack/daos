#!/bin/bash

# This is a post test processing script for post processing the
# test_daos.sh stage CI run

set -uex

test_log_dir="${1:-}"
first_node="${NODELIST%%,*}"

if [ -z "${test_log_dir}" ]; then
    echo "test_daos_post: The test log directory argument is missing!"
    exit 1
fi

# Copy logs from test_daos_node.sh execution
rm -rf "${test_log_dir}"
mkdir -p "${test_log_dir}/configs"
rsync -v -dpt -z -e "ssh ${SSH_KEY_ARGS}" jenkins@"${first_node}":/etc/daos/ \
      --filter="include daos_*.yml*" \
      --filter="exclude *" "${test_log_dir}/configs/" || true
mkdir -p "${test_log_dir}/logs"
rsync -v -dpt -z -e "ssh ${SSH_KEY_ARGS}" jenkins@"${first_node}":/tmp/ \
      --filter="include libdaos_control.log" \
      --filter="include daos_*.log*" \
      --filter="exclude *" "${test_log_dir}/logs/" || true
rsync -v -dpt -z -e "ssh ${SSH_KEY_ARGS}" jenkins@"${first_node}":/tmp/ \
      --filter="include test_daos_rpms.log" \
      --filter="exclude *" "${test_log_dir}/" || true
