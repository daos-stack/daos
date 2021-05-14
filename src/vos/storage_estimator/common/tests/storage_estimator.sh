#!/bin/bash

TEST_DIR=$(mktemp -d)
CURRENT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_DIR="${CURRENT_DIR}/../../../../.."

SETUP_FILE=setup_local.sh

pushd "${PROJECT_DIR}"

if ! [ -f "${PROJECT_DIR}/utils/sl/${SETUP_FILE}" ]; then
  echo "Error: cannot find ${SETUP_FILE} file"
  exit 1
fi

source "${PROJECT_DIR}/utils/sl/${SETUP_FILE}"

popd

function print_header {
  echo
  printf '%80s\n' | tr ' ' =
  echo "          ${1}"
  printf '%80s\n' | tr ' ' =
  echo
}

function check_retcode(){
  exit_code=${1}
  last_command=${2}

  rm -rf "${TEST_DIR}"

  if [[ ${exit_code} -eq 0 ]] ; then
    exit 0
  fi

  echo "${last_command} command exited with exit code ${exit_code}."
  exit "${exit_code}"
}
trap 'check_retcode $? ${BASH_COMMAND}' EXIT

set -e


print_header "Storage Estimator: Unit Testing"

if ! [ -x "$(command -v pytest)" ]; then
  echo "pytest not found, skipping unit testing"
else
  python -m pytest -m ut -vv "${CURRENT_DIR}"
  python -m pytest -m sx -vv "${CURRENT_DIR}"
  python -m pytest -m rp3gx -vv "${CURRENT_DIR}"
  python -m pytest -m ec16p2 -vv "${CURRENT_DIR}"
fi


print_header "Storage Estimator: Smoke Testing"

VOS_SIZE="${TEST_DIR}/vos_size_test.yaml"
DFS_SAMPLE="${TEST_DIR}/vos_dfs_sample_test.yaml"

daos_storage_estimator.py -h


print_header "Storage Estimator: create_example"

daos_storage_estimator.py create_example -h
daos_storage_estimator.py create_example -v -m "${VOS_SIZE}" -f "${DFS_SAMPLE}"


print_header "Storage Estimator: read_csv"

CLIENT_CSV="${CURRENT_DIR}/test_files/test_data.csv"
CLIENT_YAML="${TEST_DIR}/test_data.yaml"

daos_storage_estimator.py read_csv -h
daos_storage_estimator.py read_csv -v "${CLIENT_CSV}" -o "${CLIENT_YAML}"
daos_storage_estimator.py read_yaml -v "${CLIENT_YAML}"
daos_storage_estimator.py read_csv -v "${CLIENT_CSV}" --file_oclass SX
daos_storage_estimator.py read_csv -v "${CLIENT_CSV}" --file_oclass RP_3GX
daos_storage_estimator.py read_csv -v "${CLIENT_CSV}" --file_oclass EC_16P2GX
daos_storage_estimator.py read_csv -v "${CLIENT_CSV}" --file_oclass SX \
--checksum crc32
daos_storage_estimator.py read_csv -v "${CLIENT_CSV}" --file_oclass RP_3GX \
--checksum crc32
daos_storage_estimator.py read_csv -v "${CLIENT_CSV}" --file_oclass EC_16P2GX \
--checksum crc32

print_header "Storage Estimator: read_yaml"

daos_storage_estimator.py read_yaml -h
daos_storage_estimator.py create_example -m "${VOS_SIZE}" -f "${DFS_SAMPLE}"
daos_storage_estimator.py read_yaml -v "${DFS_SAMPLE}"
daos_storage_estimator.py read_yaml -v -m "${VOS_SIZE}" "${DFS_SAMPLE}"

print_header "Storage Estimator: explore_fs"

FS_YAML="${TEST_DIR}/test_fs_data.yaml"

daos_storage_estimator.py explore_fs -h
daos_storage_estimator.py explore_fs -v "${TEST_DIR}"
daos_storage_estimator.py explore_fs -v -m "${VOS_SIZE}" "${TEST_DIR}"
daos_storage_estimator.py explore_fs -v "${TEST_DIR}" -o "${FS_YAML}"
daos_storage_estimator.py read_yaml "${FS_YAML}"
daos_storage_estimator.py explore_fs -v -x "${TEST_DIR}"
daos_storage_estimator.py explore_fs -v -x -m "${VOS_SIZE}" "${TEST_DIR}"
daos_storage_estimator.py explore_fs -v -x "${TEST_DIR}" -o "${FS_YAML}"
daos_storage_estimator.py read_yaml "${FS_YAML}"
daos_storage_estimator.py explore_fs -v "${TEST_DIR}" --file_oclass SX
daos_storage_estimator.py explore_fs -v "${TEST_DIR}" --file_oclass RP_3GX
daos_storage_estimator.py explore_fs -v "${TEST_DIR}" --file_oclass EC_16P2GX
daos_storage_estimator.py explore_fs -v "${TEST_DIR}" --file_oclass SX \
--checksum crc32
daos_storage_estimator.py explore_fs -v "${TEST_DIR}" --file_oclass RP_3GX \
--checksum crc32
daos_storage_estimator.py explore_fs -v "${TEST_DIR}" --file_oclass EC_16P2GX \
--checksum crc32

print_header "Storage Estimator: Successful"
