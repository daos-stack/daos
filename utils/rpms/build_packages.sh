#!/bin/bash
set -eEuox pipefail

: "${PYTHON_VERSION:=}"

build_type="${1:-all}"
source utils/sl/setup_local.sh
echo "PYTHON_VERSION=$PYTHON_VERSION"
if [[ "${build_type}" =~ deps|all ]]; then
  utils/rpms/argobots.sh
  utils/rpms/fused.sh
  utils/rpms/isa-l.sh
  utils/rpms/isa-l_crypto.sh
  utils/rpms/libfabric.sh
  utils/rpms/mercury.sh
  utils/rpms/pmdk.sh
  utils/rpms/daos-spdk.sh
fi
if [[ "${build_type}" =~ daos|all ]]; then
  PYTHON_VERSION=\"${PYTHON_VERSION}\" \
  utils/rpms/daos.sh
fi
