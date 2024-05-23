#!/bin/bash

set -eux

cluster_reboot () {
    # shellcheck disable=SC2029,SC2089
    clush -B -S -o '-i ci_key' -l root -w "${tnodes}" reboot || true

    # shellcheck disable=SC2029,SC2089
    poll_cmd=( clush -B -S -o "-i ci_key" -l root -w "${tnodes}" )
    poll_cmd+=( cat /etc/os-release )
    reboot_timeout=600 # 10 minutes
    retry_wait=10 # seconds
    timeout=$((SECONDS + reboot_timeout))
    while [ "$SECONDS" -lt "$timeout" ]; do
      if "${poll_cmd[@]}"; then
        return 0
      fi
      sleep ${retry_wait}
    done
    return 1
}

tnodes=$(echo "$NODELIST" | cut -d ',' -f 1-"$NODE_COUNT")

rm -rf "${STAGE_NAME:?ERROR: STAGE_NAME is not defined}/"
mkdir "${STAGE_NAME:?ERROR: STAGE_NAME is not defined}/"

# shellcheck disable=SC2029,SC2089
clush -B -S -o '-i ci_key' -l root -w "${tnodes}"   \
      "DAOS_PKG_VERSION=$DAOS_PKG_VERSION           \
       STORAGE_PREP_OPT=reset                       \
       $(cat ci/storage/test_main_storage_prepare_node.sh)"

cluster_reboot

# shellcheck disable=SC2029,SC2089
clush -B -S -o '-i ci_key' -l root -w "${tnodes}"   \
      "DAOS_PKG_VERSION=$DAOS_PKG_VERSION           \
      STORAGE_PREP_OPT=prepare                      \
      $(cat ci/storage/test_main_storage_prepare_node.sh)"

cluster_reboot

# shellcheck disable=SC2029,SC2089
clush -B -S -o '-i ci_key' -l root -w "${tnodes}"   \
    "DAOS_PKG_VERSION=$DAOS_PKG_VERSION             \
    STORAGE_PREP_OPT=prepare                        \
    STORAGE_SCAN=true                               \
    $(cat ci/storage/test_main_storage_prepare_node.sh)"
