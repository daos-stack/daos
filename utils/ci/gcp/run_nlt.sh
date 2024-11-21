#!/usr/bin/env bash

set -Exo pipefail

source ./utils/ci/gcp/helper.sh

function copy_artifacts() {
  echo "Copying test artifacts to ${ARTIFACTS}"

  cp "nlt-junit.xml" "${ARTIFACTS}/junit_results.xml"
  tar -zcvf nlt-memcheck.tgz dnt.*.xml
  cp nlt-memcheck.tgz "${ARTIFACTS}"
  tar -zcvf nlt-logs.tgz /tmp/dnt_*.log*
  cp nlt-logs.tgz "${ARTIFACTS}"
  cp "nlt-errors.json" "${ARTIFACTS}"
  cp "nlt-server-leaks.json" "${ARTIFACTS}"
}

function run_nlt() {
  check_test_user

  echo "Running node local tests..."

  bash -c ". ./utils/sl/setup_local.sh; ./utils/setup_daos_server_helper.sh"
  chown -R "${TEST_USER}" .
  runuser - "${TEST_USER}" -c "cd ${PWD}; source venv/bin/activate; ./utils/node_local_test.py --memcheck=no all"
}

function main() {
  build_daos
  create_test_user
  (run_nlt)
  copy_artifacts
  go run ./utils/ci/gcp/get_nlt_results.go --file-name="./nlt-junit.xml"
  success=$?

  return $success
}

main "$@"
