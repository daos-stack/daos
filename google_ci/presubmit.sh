#!/usr/bin/env bash

set -Exo pipefail

export CMOCKA_XML_FILE="test_results/%g.xml"
export CMOCKA_MESSAGE_OUTPUT=xml

UTEST_LOG_DIR="/tmp/daos_utest"

function copy_artifacts() {
  echo "Copying test artifacts to ${ARTIFACTS}"
  cp "config.log" "${ARTIFACTS}"
  cp -r "${UTEST_LOG_DIR}" "${ARTIFACTS}"
  cp -r "./test_results" "${ARTIFACTS}"
  go run ./google_ci/coalesce.go --results-dir="./test_results" --output-file="${ARTIFACTS}/junit_results.xml"
}

function build_and_run() {
  set -e

  echo "Installing packages..."
  dnf install -y sudo git virtualenv epel-release dnf-plugins-core procps-ng
  dnf config-manager --enable powertools
  dnf config-manager --save --setopt=assumeyes=True

  echo "Running install script..."
  utils/scripts/install-el8.sh

  echo "Installing go dependencies..."
  go install gotest.tools/gotestsum@latest

  echo "Installing python dependencies..."
  virtualenv venv
  source venv/bin/activate
  pip install --require-hashes -r google_build/base_requirements.txt
  pip install --require-hashes -r google_build/requirements.txt

  echo "Building daos..."
  scons BUILD_TYPE=dev TARGET_TYPE=release --jobs="$(nproc --all)" --build-deps=yes install PREFIX=/opt/daos

  echo "Running unit tests..."
  ./utils/run_utest.py --log_dir="${UTEST_LOG_DIR}" --sudo=no --suite_filter="[^control]"
}

function main() {
  (build_and_run)
  success=$?
  copy_artifacts

  return $success
}

main "$@"
