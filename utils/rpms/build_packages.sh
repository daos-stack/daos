#!/bin/bash
set -eEuo pipefail
build_type="${1:-all}"
code_coverage="{$2:-false}"
source utils/sl/setup_local.sh
if [[ "${build_type}" =~ deps|all ]]; then
  utils/rpms/argobots.sh
  utils/rpms/fused.sh
  utils/rpms/isa-l.sh
  utils/rpms/isa-l_crypto.sh
  utils/rpms/libfabric.sh
  utils/rpms/mercury.sh
  utils/rpms/pmdk.sh
  utils/rpms/spdk.sh
  if [ "${code_coverage}" != "false" ]; then
    utils/rpms/bullseye.sh
  fi
fi
if [[ "${build_type}" =~ daos|all ]]; then
  utils/rpms/daos.sh
fi
