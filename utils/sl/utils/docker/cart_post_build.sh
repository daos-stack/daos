#!/bin/bash

test_results=$(find . -name test_output -print -quit)

ls "${test_results}"
set +e
grep FAILED "${test_results}"
rc=$?
set -e
if [ ${rc} == 0 ]; then
  # Fail this build.
  exit 1
fi
grep "All tests passed" docker_build.log
