#!/bin/bash

# Common cleanup for running after a functional test

set -ex

if $TEST_RPMS; then
    # now collect up the logs and store them like non-RPM test does
    mkdir -p install/lib/daos/TESTING/
    first_node=${NODELIST%%,*}
    # scp doesn't copy symlinks, it resolves them
    ssh -i ci_key -l jenkins "${first_node}" tar -C /var/tmp/ -czf - ftest |
        tar -C install/lib/daos/TESTING/ -xzf -
fi

echo '========='
pwd
if [ -e "install/lib/daos/TESTING/ftest/avocado/job-results/${STAGE_NAME}" ]; then
    mv "install/lib/daos/TESTING/ftest/avocado/job-results/${STAGE_NAME}/"* "${STAGE_NAME}/"
else
    echo "No avocado job-results found!"
    ls -al install/lib/daos/TESTING/ftest/avocado/job-results
    exit 1
fi
