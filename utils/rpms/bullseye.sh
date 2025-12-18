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
FILTER_LIST=("${SL_BULLSEYE_PREFIX}/sample")
readarray -t dir_list < <(find "${SL_BULLSEYE_PREFIX}" -mindepth 1 -maxdepth 1 -type d)
for dir in "${dir_list[@]}"; do
    if filter_file "${dir}"; then
      continue
    fi
    readarray -t dir_file_list < <(find "${dir}" -mindepth 1 -maxdepth 1 -type f)
    TARGET_PATH="${dir}"
    for dir_file in "${dir_file_list[@]}"; do
        list_files files "${dir_file}"
        append_install_list "${files[@]}"
    done
done
TARGET_PATH="${SL_BULLSEYE_PREFIX}/daos"
list_files files "test.cov"
append_install_list "${files[@]}"

cat << EOF  > "${tmp}/post_install_bullseye"
chmod 666 ${SL_BULLSEYE_PREFIX}/daos/test.cov
EOF
EXTRA_OPTS+=("--after-install" "${tmp}/post_install_bullseye")

build_package "bullseye"
