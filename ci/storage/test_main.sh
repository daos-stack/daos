#!/bin/bash

set -eux

tnodes=$(echo "$NODELIST" | cut -d ',' -f 1-"$NODE_COUNT")

rm -rf "${STAGE_NAME:?ERROR: STAGE_NAME is not defined}/"
mkdir "${STAGE_NAME:?ERROR: STAGE_NAME is not defined}/"

# shellcheck disable=SC2029,SC2089
clush -B -S -o '-i ci_key' -l root -w "${tnodes}"   \
      "DAOS_PKG_VERSION=$DAOS_PKG_VERSION           \
      $(cat ci/storage/test_main_storage_prepare_node.sh)"

# shellcheck disable=SC2029,SC2089
clush -B -S -o '-i ci_key' -l root -w "${tnodes}" reboot || true

ready_command="cat /etc/os-release"
# shellcheck disable=SC2029,SC2089
poll_cmd="clush -B -S -o '-i ci_key' -l root -w ${tnodes} ${ready_command}"
reboot_timeout=600 # 10 minutes
retry_wait=10 # seconds
timeout=${SECONDS+$reboot_timeout}
while [ "$SECONDS" -lt "$timeout" ]; do
  # shellcheck disable=SC2090
  if ${poll_cmd}; then
    break
  fi
  sleep ${retry_wait}
done

# shellcheck disable=SC2029,SC2089
clush -B -S -o '-i ci_key' -l root -w "${tnodes}"   \
    "DAOS_PKG_VERSION=$DAOS_PKG_VERSION             \
    $(cat ci/storage/test_main_storage_prepare_node.sh)"
