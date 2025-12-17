#!/bin/bash
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

# Temporary
SL_BULLSEYE_PREFIX="${SL_BULLSEYE_PREFIX:-/opt/BullseyeCoverage}"

if [ -z "${SL_BULLSEYE_PREFIX:-}" ]; then
  echo "bullseye must be installed or never built in ${SL_BULLSEYE_PREFIX:-}"
  exit 0
fi

VERSION="${bullseye_version}"
RELEASE="${bullseye_release}"
LICENSE="Proprietary"
ARCH="${isa}"
DESCRIPTION="The BullseyeCoverage compiler"
URL="https://www.bullseye.com/index.html"
RPM_CHANGELOG="bullseye.changelog"

PACKAGE_TYPE="dir"
files=()
TARGET_PATH="${SL_BULLSEYE_PREFIX}"

readarray -t bullseye_dirs < <(find /opt/BullseyeCoverage -mindepth 1 -maxdepth 1 -type d)
for dir in "${bullseye_dirs[@]}"; do
    readarray -t dir_files < <(find "${dir}" -mindepth 1 -maxdepth 1 -type f)
    for dir_file in "${dir_files[@]}"; do
        list_files files "${dir_file}"
        append_install_list "${files[@]}"
    done
done

build_package "bullseye"
