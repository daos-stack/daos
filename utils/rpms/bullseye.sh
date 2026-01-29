#!/bin/bash
#
# Copyright 2026 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Script for building the bullseye rpm package
set -eEuo pipefail

root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

: "${SL_BULLSEYE_PREFIX:=/opt/BullseyeCoverage}"
if [ ! -d "${SL_BULLSEYE_PREFIX}" ]; then
  echo "bullseye must be installed or built in ${SL_BULLSEYE_PREFIX}"
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

# Add bullseye files
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

# Add test.cov file
TARGET_PATH="${SL_BULLSEYE_PREFIX}/daos"
list_files files "test.cov"
append_install_list "${files[@]}"

# Create tar file containing all source files for the covhtml command
readarray -t src_file_list < <("${SL_BULLSEYE_PREFIX}/bin/covmgr" -l --file test.cov)
if [ ${#src_file_list[@]} -gt 0 ]; then
  tar -czf "${tmp}/bullseye_sources.tar.gz" "${src_file_list[@]}" 2>/dev/null || {
    echo "Warning: Some source files may not exist, creating tar with existing files only"
    existing_files=()
    for src_file in "${src_file_list[@]}"; do
      if [ -f "${src_file}" ]; then
        existing_files+=("${src_file}")
      fi
    done
    if [ ${#existing_files[@]} -gt 0 ]; then
      tar -czf "${tmp}/bullseye_sources.tar.gz" "${existing_files[@]}"
      echo "Created tar file with ${#existing_files[@]} existing source files"
    else
      echo "No source files found to archive"
    fi
  }
else
  echo "No source files found in src_file_list"
fi
list_files files "${tmp}/bullseye_sources.tar.gz"
append_install_list "${files[@]}"

# Add sources for covhtml command
for src_file in "${src_file_list[@]}"; do
  dir_name=$(dirname "${src_file}")
  TARGET_PATH="${SL_BULLSEYE_PREFIX}/daos/${dir_name}"
  list_files files "${src_file}"
  append_install_list "${files[@]}"
done

# Fix file permissions
cat << EOF  > "${tmp}/post_install_bullseye"
chmod 666 ${SL_BULLSEYE_PREFIX}/daos/test.cov
chmod 666 ${SL_BULLSEYE_PREFIX}/daos/bullseye_sources.tar.gz
EOF
EXTRA_OPTS+=("--after-install" "${tmp}/post_install_bullseye")

build_package "bullseye"
