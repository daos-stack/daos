#!/bin/bash

# Common cleanup for running after a functional test

set -ex

rm -rf install/lib/daos/TESTING/ftest/avocado/job-results/job-*/html/

# Remove the latest avocado symlink directory to avoid inclusion in the
# jenkins build artifacts
if [ -e install/lib/daos/TESTING/ftest/avocado/job-results/latest ]; then
  unlink install/lib/daos/TESTING/ftest/avocado/job-results/latest
fi

# compress those potentially huge DAOS logs
find install/lib/daos/TESTING/ftest/avocado/job-results/job-*/daos_logs/* \
  -maxdepth 0 -type f -size +1M -print0 | xargs -r0 lbzip2

arts="$arts$(ls ./*daos{,_agent}.log* 2>/dev/null)" && arts="$arts"$'\n'
arts="$arts$(ls -d \
   install/lib/daos/TESTING/ftest/avocado/job-results/job-* 2>/dev/null)" && \
  arts="$arts"$'\n'
if [ -n "$arts" ]; then
  # shellcheck disable=SC2046,SC2086
  mv $(echo $arts | tr '\n' ' ') "Functional/"
fi
