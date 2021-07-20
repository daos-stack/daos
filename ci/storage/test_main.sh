#!/bin/bash

set -eux

tnodes=$(echo "$NODELIST" | cut -d ',' -f 1-"$NODE_COUNT")
# first_node=${NODELIST%%,*}

# clush -B -S -o '-i ci_key' -l root -w "${first_node}" \
#     "NODELIST=${NODELIST} $(cat ci/functional/setup_nfs.sh)"

# clush -B -S -o '-i ci_key' -l root -w "${tnodes}" \
#  "OPERATIONS_EMAIL=${OPERATIONS_EMAIL}                \
#   FIRST_NODE=${first_node}                            \
#   TEST_RPMS=${TEST_RPMS}                              \
#   $(cat ci/functional/test_main_prep_node.sh)"

# this is being mis-flagged as SC2026 where shellcheck.net is OK with it
# shellcheck disable=SC2026
#trap 'clush -B -S -o "-i ci_key" -l root -w "${tnodes}" '\
#'"set -x; umount /mnt/share"' EXIT

# Setup the Jenkins build artifacts directory before running the tests to ensure
# there is enough disk space to report the results.
rm -rf "${STAGE_NAME:?ERROR: STAGE_NAME is not defined}/"
mkdir "${STAGE_NAME:?ERROR: STAGE_NAME is not defined}/"

# set DAOS_TARGET_OVERSUBSCRIBE env here
# export DAOS_TARGET_OVERSUBSCRIBE=1
# rm -rf install/lib/daos/TESTING/ftest/avocado ./*_results.xml
# mkdir -p install/lib/daos/TESTING/ftest/avocado/job-results
#if $TEST_RPMS; then
    # shellcheck disable=SC2029,SC2089
    clush -B -S -o '-i ci_key' -l root -w "${tnodes}"   \
      "DAOS_PKG_VERSION=$DAOS_PKG_VERSION            \
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
      "DAOS_PKG_VERSION=$DAOS_PKG_VERSION            \
       $(cat ci/storage/test_main_storage_prepare_node.sh)"

#else
#    echo "Currently only supported with test RPMS"
#fi
