#!/bin/bash
# (C) Copyright 2025 Google LLC
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_ARGOBOTS_PREFIX}" ]; then
  echo "Argbots must be installed or never built"
  exit 0
fi

VERSION="${argobots_version}"
RELEASE="2"
LICENSE="UChicago Argonne, LLC -- Argobots License"
DESCRIPTION="Argobots is a lightweight, low-level threading and tasking framework.
This release is an experimental version of Argobots that contains
features related to user-level threads, tasklets, and some schedulers."
URL="https://argobots.org"

files=()
TARGET_PATH="${libdir}"
list_files files "${SL_ARGOBOTS_PREFIX}/lib64/libabt.so.*"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

ARCH="${isa}"
build_package "${argobots_lib}"

if [ "${BUILD_EXTRANEOUS:-no}" = "yes" ]; then
  TARGET_PATH="${libdir}/pkgconfig"
  list_files files "${SL_ARGOBOTS_PREFIX}/lib64/pkgconfig/*"
  replace_paths "${SL_ARGOBOTS_PREFIX}" "${files[@]}"
  append_install_list "${files[@]}"

  TARGET_PATH="${libdir}"
  list_files files "${SL_ARGOBOTS_PREFIX}/lib64/libabt.so"
  append_install_list "${files[@]}"

  TARGET_PATH="${includedir}"
  list_files files "${SL_ARGOBOTS_PREFIX}/include/abt.h"
  append_install_list "${files[@]}"

  DEPENDS=("${argobots_lib} = ${argobots_version}-${RELEASE}")
  build_package "${argobots_dev}"
fi
