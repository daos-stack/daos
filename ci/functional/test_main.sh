#!/bin/bash

set -eux

if [ -z "$TEST_TAG" ]; then
    echo "TEST_TAG must be set"
    exit 1
fi

test_tag="$TEST_TAG"

tnodes=$(echo "$NODELIST" | cut -d ',' -f 1-"$NODE_COUNT")
first_node=${NODELIST%%,*}

cluster_reboot () {
    # shellcheck disable=SC2029,SC2089
    clush -B -S -o '-i ci_key' -l root -w "${tnodes}" reboot || true

    # shellcheck disable=SC2029,SC2089
    poll_cmd=( clush -B -S -o "-i ci_key" -l root -w "${tnodes}" )
    poll_cmd+=( cat /etc/os-release )
    reboot_timeout=900 # 15 minutes
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
        $(cat ci/functional/test_main_prep_node.sh)"
}

clush -B -S -o '-i ci_key' -l root -w "${first_node}" \
    "NODELIST=${NODELIST} $(cat ci/functional/setup_nfs.sh)"

if ! test_cluster; then
    # Sometimes a cluster reboot will fix the issue so try it once.
    cluster_reboot
    test_cluster
fi

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
if $TEST_RPMS; then
    # shellcheck disable=SC2029
    ssh -i ci_key -l jenkins "${first_node}" \
      "TEST_TAG=\"$test_tag\"                        \
       TNODES=\"$tnodes\"                            \
       FTEST_ARG=\"$FTEST_ARG\"                      \
       WITH_VALGRIND=\"$WITH_VALGRIND\"              \
       STAGE_NAME=\"$STAGE_NAME\"                    \
       $(cat ci/functional/test_main_node.sh)"
else
    ./ftest.sh "$test_tag" "$tnodes" "$FTEST_ARG"
fi
