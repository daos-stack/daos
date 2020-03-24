#!/bin/bash

test_results="docker_build.log"

ls "${test_results}"
set +e
grep "This run had test failures" "${test_results}"
rc=$?
set -e
if [ ${rc} == 0 ]; then
  # Fail this build.
  exit 1
fi
grep "All tests passed" "${test_results}"

