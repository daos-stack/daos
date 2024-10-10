#!/bin/bash
#
#  Copyright 2020-2023 Intel Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent

set -eux

if [ -z "$TEST_TAG" ]; then
    echo "TEST_TAG must be set"
    exit 1
fi

test_tag="$TEST_TAG"

tnodes=$(echo "$NODELIST" | cut -d ',' -f 1-"$NODE_COUNT")
first_node=${NODELIST%%,*}

hardware_ok=false

cluster_reboot () {
    # shellcheck disable=SC2029,SC2089
    clush -B -S -o '-i ci_key' -l root -w "${tnodes}" reboot || true

    # shellcheck disable=SC2029,SC2089
    poll_cmd=( clush -B -S -o "-i ci_key" -l root -w "${tnodes}" )
    poll_cmd+=( cat /etc/os-release )
    # 20 minutes, HPE systems may take more than 15 minutes.
    reboot_timeout=1200
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

test_cluster() {
    # Test that all nodes in the cluster are healthy
    clush -B -S -o '-i ci_key' -l root -w "${tnodes}"   \
        "OPERATIONS_EMAIL=${OPERATIONS_EMAIL}           \
        FIRST_NODE=${first_node}                        \
        TEST_RPMS=${TEST_RPMS}                          \
        NODELIST=${tnodes}                              \
        BUILD_URL=\"${BUILD_URL:-Unknown in GHA}\"      \
        STAGE_NAME=\"$STAGE_NAME\"                      \
        $(cat ci/functional/test_main_prep_node.sh)"
}

clush -B -S -o '-i ci_key' -l root -w "${first_node}" \
    "NODELIST=${NODELIST} $(cat ci/functional/setup_nfs.sh)"

if ! test_cluster; then
    # Sometimes a cluster reboot will fix the issue so try it once.
    if cluster_reboot; then
        if test_cluster; then
            hardware_ok=true
        fi
    fi
else
    hardware_ok=true
fi

# collect the _results.xml files from test_main_prep_nodes before they
# may be deleted by pre-test cleanup.
# Cannot use a wildcard for collection as that just ends up
# with a wild card in the collected filename which just makes
# things more confusing.
rm -f ./hardware_prep_node_results.xml.* ./hardware_prep_node_*_results.xml
clush -o '-i ci_key' -l root -w "$tnodes" \
      --rcopy hardware_prep_node_results.xml
# This results in file names with the node name as the suffix.

# this is being mis-flagged as SC2026 where shellcheck.net is OK with it
# shellcheck disable=SC2026
trap 'clush -B -S -o "-i ci_key" -l root -w "${tnodes}" '\
'"set -x; umount /mnt/share"' EXIT

# Setup the Jenkins build artifacts directory before running the tests to ensure
# there is enough disk space to report the results.
rm -rf "${STAGE_NAME:?ERROR: STAGE_NAME is not defined}/"
mkdir "${STAGE_NAME:?ERROR: STAGE_NAME is not defined}/"

# set DAOS_TARGET_OVERSUBSCRIBE env here
export DAOS_TARGET_OVERSUBSCRIBE=1
rm -rf install/lib/daos/TESTING/ftest/avocado ./*_results.xml

mkdir -p install/lib/daos/TESTING/ftest/avocado/job-results

if "$hardware_ok"; then
    if $TEST_RPMS; then
        # shellcheck disable=SC2029
        ssh -i ci_key -l jenkins "${first_node}"   \
          "TEST_TAG=\"$test_tag\"                  \
           TNODES=\"$tnodes\"                      \
           FTEST_ARG=\"${FTEST_ARG:-}\"            \
           WITH_VALGRIND=\"${WITH_VALGRIND:-}\"    \
           STAGE_NAME=\"$STAGE_NAME\"              \
           $(cat ci/functional/test_main_node.sh)"
    else
        ./ftest.sh "$test_tag" "$tnodes" "$FTEST_ARG"
    fi
fi

# Now rename the previously collected hardware test data for Jenkins
# to use them for Junit processing.
: "${STAGE_NAME:=}"
mkdir -p "${STAGE_NAME}/hardware_prep/"
for node in ${tnodes//,/ }; do
    old_name="./hardware_prep_node_results.xml.$node"
    new_name="${STAGE_NAME}/hardware_prep/${node}/results.xml"
    if [ -e "$old_name" ]; then
        mkdir -p "${STAGE_NAME}/hardware_prep/${node}"
        mv "$old_name" "$new_name"
    fi
done
"$hardware_ok"
