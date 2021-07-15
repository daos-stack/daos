#!/bin/bash

set -eux

if [ -z "$TEST_TAG" ]; then
    echo "TEST_TAG must be set"
    exit 1
fi

test_tag="$TEST_TAG"

tnodes=$(echo "$NODELIST" | cut -d ',' -f 1-"$NODE_COUNT")
first_node=${NODELIST%%,*}

clush -B -S -o '-i ci_key' -l root -w "${first_node}" \
    "NODELIST=${NODELIST} $(cat ci/functional/setup_nfs.sh)"

clush -B -S -o '-i ci_key' -l root -w "${tnodes}" \
  "OPERATIONS_EMAIL=${OPERATIONS_EMAIL}                \
   FIRST_NODE=${first_node}                            \
   TEST_RPMS=${TEST_RPMS}                              \
   $(cat ci/functional/test_main_prep_node.sh)"

# this is being mis-flagged as SC2026 where shellcheck.net is OK with it
# shellcheck disable=SC2026
trap 'clush -B -S -o "-i ci_key" -l root -w "${tnodes}" '\
'"set -x; umount /mnt/share"' EXIT

# Setup the Jenkins build artifacts directory before running the tests to ensure
# there is enough disk space to report the results.
rm -rf "${STAGE_NAME:?ERROR: STAGE_NAME is not defined}/"
mkdir "${STAGE_NAME:?ERROR: STAGE_NAME is not defined}/"

# run node checkout
if false; then
    clush -B -S -o '-i ci_key' -l root -w "${tnodes}" -c ci/functional/fio_libpmem.fio --dest=/tmp/

    clush -B -S -o '-i ci_key' -l root -w "${tnodes}" \
        "$(cat ci/functional/node_checkout.sh)"
fi


# check ssd smart
if false; then
    tnodes2="wolf-110,wolf-111"
    clush -B -S -o '-i ci_key' -l root -w "${tnodes2}" \
        "$(cat ci/functional/smart_health.sh)"
fi


# run network test
# create log directory
if false; then
    clush -B -S -o '-i ci_key' -l root -w "${tnodes}" \
        "mkdir -p /var/tmp/daos_testing && chown jenkins /var/tmp/daos_testing"
    run_on_node=$(echo ${tnodes} | cut -d ',' -f 2)
    clush -B -S -o '-i ci_key' -l jenkins -w "${run_on_node}" \
        "daospath='/usr/'                                 \
         nodes=${tnodes}                                  \
         $(cat ci/functional/self_test_8-node.sh)"
fi

# Override tnodes for debugging which node(s) is the culprit
# wolf-[51,110-117]
tnodes="wolf-51,wolf-110,wolf-111"

# set DAOS_TARGET_OVERSUBSCRIBE env here
export DAOS_TARGET_OVERSUBSCRIBE=1
rm -rf install/lib/daos/TESTING/ftest/avocado ./*_results.xml
mkdir -p install/lib/daos/TESTING/ftest/avocado/job-results
if true; then
    if $TEST_RPMS; then
        # shellcheck disable=SC2029
        ssh -i ci_key -l jenkins "${first_node}" \
          "TEST_TAG=\"$test_tag\"                        \
           TNODES=\"$tnodes\"                            \
           FTEST_ARG=\"$FTEST_ARG\"                      \
           $(cat ci/functional/test_main_node.sh)"
    else
        ./ftest.sh "$test_tag" "$tnodes" "$FTEST_ARG"
    fi
fi
