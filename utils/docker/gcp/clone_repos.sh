#!/bin/bash

BASH_DIR=$(dirname "${BASH_SOURCE[0]}")
SCRIPT_DIR=$(readlink -f "${BASH_DIR}")
GET_HASH="${SCRIPT_DIR}/get_rpm_hash.sh"

get_correct_version()
{
  name=$1
  directory=$1
  if [ $# -gt 1 ]; then
    name=$2
  fi

  git clone "https://github.com/daos-stack/${directory}.git"
  pushd "${directory}"
  commit=$("${GET_HASH}" "${name}")
  git checkout "${commit}"
  popd
}

set -e

get_correct_version "libfabric"
get_correct_version "isa-l" "isal"
get_correct_version "isa-l_crypto" "isal_crypto"
get_correct_version "mercury"
get_correct_version "argobots"
get_correct_version "raft"

# for now, we build server rpms in el8 build
if [ "${1:-client}" = "server" ]; then
  get_correct_version "dpdk"
  get_correct_version "spdk"
  get_correct_version "pmdk"
fi
