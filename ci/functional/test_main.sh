#!/bin/bash

set -eux

test_tag=$(git show -s --format=%B | \
           sed -ne "/^Test-tag$PRAGMA_SUFFIX:/s/^.*: *//p")
if [ -z "$test_tag" ]; then
    # shellcheck disable=SC2153
    test_tag=$TEST_TAG
fi

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
rm -rf "Functional/"
mkdir "Functional/"

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
       $(cat ci/functional/test_main_node.sh)"
else
    ./ftest.sh "$test_tag" "$tnodes" "$FTEST_ARG"
fi
