#!/bin/bash
# Copyright 2025 Google LLC
# Copyright 2026 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -eEuo pipefail

usage() {
  cat <<EOF
Usage: ${0##*/} [options] [PKG_OUTPUT_DIR]

Build DAOS/dependency packages with fpm, verifying them afterwards.

Args:
    PKG_OUTPUT_DIR - Full path under which "deps" and "daos" package dirs are
                     created and populated.
                     Default for RPM builds: <repo_root>/rpms; for non-RPM
                     builds: .

Options:
    --build-range=[deps|daos|all] - What to build. Default: all
    --rpm-suffix=[el9|suse.lp155|suse.lp156] - RPM distribution suffix.
        Auto-detected from /etc/os-release when omitted.
    [ -Werror | -Wno-error ] - Select whether validation findings are treated
        as errors or warnings. The options are mutually exclusive.
        Default: -Werror.
    -h, --help - Show this help and exit

Environment:
    OUTPUT_TYPE  Package format to build: rpm|deb. Default: rpm.
EOF
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
repo_root="$(cd "${script_dir}/../.." >/dev/null 2>&1 && pwd)"
# shellcheck source=utils/build/build_utils.sh
source "${script_dir}/build_utils.sh"

build_range=all
rpm_suffix=
verify_mode=
positional_args=()
for arg in "$@"; do
  case "${arg}" in
    --help | -h)
      usage
      exit 0
      ;;
    --build-range=*)
      build_range="${arg#*=}"
      ;;
    --rpm-suffix=*)
      rpm_suffix="${arg#*=}"
      ;;
    -Werror | -Wno-error)
      [ -z "${verify_mode}" ] || {
        echo "ERROR: -Werror and -Wno-error are mutually exclusive" >&2
        exit 1
      }
      verify_mode="${arg}"
      ;;
    --*)
      echo "ERROR: unknown option '${arg}'" >&2
      usage >&2
      exit 1
      ;;
    *)
      positional_args+=("${arg}")
      ;;
  esac
done
verify_mode="${verify_mode:--Werror}"

if [ "${#positional_args[@]}" -gt 1 ]; then
  echo "ERROR: at most one PKG_OUTPUT_DIR argument is allowed" >&2
  usage >&2
  exit 1
fi

case "${build_range}" in
  deps | daos | all)
    ;;
  *)
    echo "ERROR: --build-range must be deps, daos, or all (got: ${build_range})" >&2
    exit 1
    ;;
esac

if [ "${OUTPUT_TYPE:-rpm}" = "rpm" ]; then
  rpm_suffix="${rpm_suffix:-$(detect_rpm_suffix)}" || exit $?
  case "${rpm_suffix}" in
    el9 | suse.lp155 | suse.lp156)
      ;;
    *)
      echo "ERROR: --rpm-suffix must be el9, suse.lp155, or suse.lp156 (got: ${rpm_suffix})" >&2
      exit 1
      ;;
  esac
  DISTRO="${rpm_suffix}"
else
  if [ -n "${rpm_suffix}" ]; then
    echo "ERROR: --rpm-suffix is only valid for RPM builds" >&2
    exit 1
  fi
  DISTRO=ubuntu
fi
export DISTRO

if [ "${#positional_args[@]}" -eq 1 ]; then
  pkg_output_dir="${positional_args[0]}"
elif [ -n "${rpm_suffix}" ]; then
  pkg_output_dir="${repo_root}/rpms"
else
  pkg_output_dir=.
fi
PACKAGE_OUTPUT_DIR="${pkg_output_dir}"
export PACKAGE_OUTPUT_DIR

prepare_rpms_stage() {
  if [ -n "${rpm_suffix}" ]; then
    unset PACKAGE_OUTPUT_DIR
    PACKAGE_OUTPUT_DIR="${pkg_output_dir}/${1}"
    rm -f "${PACKAGE_OUTPUT_DIR}"/*.rpm
    mkdir -p "${PACKAGE_OUTPUT_DIR}"
    export PACKAGE_OUTPUT_DIR
  fi
}

# Runs verify_packages.sh against $pkg_output_dir/$1 when applicable.
verify_rpm_stage() {
  if [ -n "${rpm_suffix}" ] &&
    compgen -G "${pkg_output_dir}/${1}/*.rpm" > /dev/null; then
    "${script_dir}/verify_packages.sh" "${verify_mode}" \
      "--rpm-suffix=${rpm_suffix}" "${pkg_output_dir}/${1}"
  fi
}

source utils/sl/setup_local.sh
if [[ "${build_range}" =~ deps|all ]]; then
  prepare_rpms_stage deps
  utils/rpms/argobots.sh
  utils/rpms/fused.sh
  utils/rpms/isa-l.sh
  utils/rpms/isa-l_crypto.sh
  utils/rpms/libfabric.sh
  utils/rpms/mercury.sh
  utils/rpms/pmdk.sh
  utils/rpms/daos-spdk.sh
  verify_rpm_stage deps
fi

if [[ "${build_range}" =~ daos|all ]]; then
  prepare_rpms_stage daos
  utils/rpms/daos.sh
  verify_rpm_stage daos
fi

if [ -n "${rpm_suffix}" ]; then
  rm -rf "${pkg_output_dir}/repodata"
  createrepo "${pkg_output_dir}"
fi
