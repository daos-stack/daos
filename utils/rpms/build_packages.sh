#!/bin/bash
# Copyright 2025 Google LLC
# Copyright 2026 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent

# Usage:
#   build_packages.sh [build_type] [verify]
#
# Args:
#   build_type  What to build: deps|daos|all. Default: all
#   verify      Run verify_rpms.sh after package build: yes|no. Default: yes
set -eEuo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
repo_root="$(cd "${script_dir}/../.." >/dev/null 2>&1 && pwd)"

build_type="${1:-all}"
verify_rpms="${2:-yes}"

case "${verify_rpms}" in
  yes|no)
    ;;
  *)
    echo "ERROR: verify parameter must be 'yes' or 'no' (got: ${verify_rpms})"
    exit 1
    ;;
esac

cd "${repo_root}"
source utils/sl/setup_local.sh
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
  utils/rpms/daos.sh
fi

case "${DISTRO:-el9}" in
  el*|suse.lp15*)
    if [ "${OUTPUT_TYPE:-rpm}" = "rpm" ] && [ "${verify_rpms}" = "yes" ]; then
      "${script_dir}/verify_rpms.sh" "$PWD" "${DISTRO:-el9}"
    fi
    ;;
esac
