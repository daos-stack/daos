#!/bin/bash

set -Exo pipefail

echo "Running unit tests..."

source ./utils/ci/gcp/helper.sh

ARTIFACTS="${ARTIFACTS_DIR}/${RESULTS_DIR}"
mkdir -p "${ARTIFACTS}"

export CMOCKA_XML_FILE="test_results/%g.xml"
export CMOCKA_MESSAGE_OUTPUT=xml

UTEST_LOG_DIR="/tmp/daos_utest"

function copy_artifacts() {
  echo "Copying test artifacts to ${ARTIFACTS}"

  # Unit test results
  cp "config.log" "${ARTIFACTS}"
  cp -r "${UTEST_LOG_DIR}" "${ARTIFACTS}"
  cp -r "./test_results" "${ARTIFACTS}"
  go run ./utils/ci/gcp/coalesce_unit_test_results.go --results-dir="./test_results" --output-file="${ARTIFACTS}/junit_results.xml"
}

function run_unit_tests() {
  echo "Running unit tests..."
  chown -R "${TEST_USER}" .
  runuser - "${TEST_USER}" -c "cd ${PWD}; \
    source venv/bin/activate; \
    export CMOCKA_XML_FILE=${CMOCKA_XML_FILE}; \
    export CMOCKA_MESSAGE_OUTPUT=${CMOCKA_MESSAGE_OUTPUT}; \
    ./utils/run_utest.py --log_dir=${UTEST_LOG_DIR} --sudo=no"
}

function main() {
  build_daos
  create_test_user
  (run_unit_tests)
  success=$?
  copy_artifacts
  return $success
}

main "$@"
