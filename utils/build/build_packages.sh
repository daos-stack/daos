#!/bin/bash
# Copyright 2025 Google LLC
# Copyright 2026 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -eEuo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
repo_root="$(cd "${script_dir}/../.." >/dev/null 2>&1 && pwd)"
# shellcheck source=utils/build/build_utils.sh
source "${script_dir}/build_utils.sh"

usage() {
  cat <<EOF
Usage: ${0##*/} [build_type] [verify]

Build DAOS/dependency RPMs with fpm, verifying them afterwards.

Args:
    build_type  What to build: deps|daos|all. Default: all
    verify      Run verify_packages.sh after package build: yes|no. Default: yes

Options:
    -h, --help  show this help and exit

Env:
    DISTRO          Target distro (e.g. el9, suse.lp156). Auto-detected from
                    /etc/os-release when unset.
    RPM_OUTPUT_DIR  Full path under which "deps" and "daos" rpm dirs are
                    created and populated. Default: repo_root/rpms.
EOF
}

check_help "$@"

if [ "$#" -gt 2 ]; then
  echo "ERROR: too many arguments"
  usage
  exit 1
fi

RPM_OUTPUT_DIR="${RPM_OUTPUT_DIR:-${repo_root}/rpms}"
DISTRO="${DISTRO:-$(detect_distro)}" || exit $?

build_type="${1:-all}"
verify_rpms="${2:-yes}"

check_yes_no() {
  case "${2}" in
    yes|no)
      ;;
    *)
      echo "ERROR: ${1} parameter must be 'yes' or 'no' (got: ${2})"
      exit 1
      ;;
  esac
}

check_yes_no verify "${verify_rpms}"

cd "${repo_root}"
rm -f ./*.rpm
mkdir -p "${RPM_OUTPUT_DIR}"

# Directs RPM output into $RPM_OUTPUT_DIR/$1. Non-RPM builds retain the default
# fpm behavior of writing packages to the current directory.
prepare_rpms_stage() {
  unset PACKAGE_OUTPUT_DIR
  if [[ "${DISTRO}" == el* || "${DISTRO}" == suse.lp15* ]]; then
    PACKAGE_OUTPUT_DIR="${RPM_OUTPUT_DIR}/${1}"
    export PACKAGE_OUTPUT_DIR
    mkdir -p "${PACKAGE_OUTPUT_DIR}"
  fi
}

# Runs verify_packages.sh against $rpm_root/$1 when applicable; failures only
# fail the build when verify=yes (ERROR mode), otherwise they're warnings.
verify_stage() {
  if [[ "${DISTRO}" == el* || "${DISTRO}" == suse.lp15* ]] &&
     [ "${OUTPUT_TYPE:-rpm}" = "rpm" ] &&
     compgen -G "${RPM_OUTPUT_DIR}/${1}/*.rpm" > /dev/null; then
    local mode="ERROR"
    [ "${verify_rpms}" = "yes" ] || mode="WARNING"
    "${script_dir}/verify_packages.sh" "${RPM_OUTPUT_DIR}/${1}" "${DISTRO}" "${mode}"
  fi
}

source utils/sl/setup_local.sh
if [[ "${build_type}" =~ deps|all ]]; then
  prepare_rpms_stage deps
  utils/rpms/argobots.sh
  utils/rpms/fused.sh
  utils/rpms/isa-l.sh
  utils/rpms/isa-l_crypto.sh
  utils/rpms/libfabric.sh
  utils/rpms/mercury.sh
  utils/rpms/pmdk.sh
  utils/rpms/daos-spdk.sh
  verify_stage deps
fi

if [[ "${build_type}" =~ daos|all ]]; then
  prepare_rpms_stage daos
  utils/rpms/daos.sh
  verify_stage daos
fi

if [[ "${build_type}" =~ deps|daos|all ]] && \
   [[ "${DISTRO}" == el* || "${DISTRO}" == suse.lp15* ]] && \
   [ "${OUTPUT_TYPE:-rpm}" = "rpm" ]; then
  rm -rf "${RPM_OUTPUT_DIR}/repodata"
  createrepo "${RPM_OUTPUT_DIR}"
fi
