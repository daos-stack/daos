#!/bin/bash

# Common cleanup for running after a functional test

set -eux

if [ -z "${NODELIST:-}" ]; then
    exit 0
fi

if $TEST_RPMS; then
    # now collect up the logs and store them like non-RPM test does
    mkdir -p install/lib/daos/TESTING/
    first_node=${NODELIST%%,*}
    # scp doesn't copy symlinks, it resolves them
    ssh -i ci_key -l jenkins "${first_node}" tar -C /var/tmp/ -czf - ftest |
        tar -C install/lib/daos/TESTING/ -xzf -
fi

rm -rf install/lib/daos/TESTING/ftest/avocado/job-results/*/*/html/

# Remove the latest avocado symlink directory to avoid inclusion in the
# jenkins build artifacts
rm -f install/lib/daos/TESTING/ftest/avocado/job-results/latest

arts="${arts:-}$(ls ./*daos{,_agent}.log* 2>/dev/null)" && arts="$arts"$'\n'
arts="$arts$(ls -d \
   install/lib/daos/TESTING/ftest/avocado/job-results/* 2>/dev/null)" && \
  arts="$arts"$'\n'
if [ -n "$arts" ]; then
    # shellcheck disable=SC2046,SC2086
    mv $(echo $arts | tr '\n' ' ') "${STAGE_NAME}/"
    hostname
    pwd
    find "${STAGE_NAME}/" -type f
fi
