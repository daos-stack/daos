#!/bin/bash
# (C) Copyright 2025 Google LLC
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_ARGOBOTS_PREFIX}" ]; then
  echo "Argbots must be installed or never built"
  exit 0
fi

files=()
headers=()
libs=()
pkgcfg=()
dbg=()

VERSION="1.2"
RELEASE="2"
LICENSE="UChicago Argonne, LLC -- Argobots License"
DESCRIPTION="Argobots is a lightweight, low-level threading and tasking framework.
This release is an experimental version of Argobots that contains
features related to user-level threads, tasklets, and some schedulers."
URL="https://argobots.org"

if [[ "${DISTRO:-}" =~ "suse" ]]; then
  argobots_lib="libabt0"
  argobots_devel="libabt-devel"
else
  argobots_lib="argobots"
  argobots_devel="argobots-devel"
fi

TARGET_PATH="${libdir}"
list_files files "${SL_ARGOBOTS_PREFIX}/lib64/libabt.so.*"
clean_bin dbg "${files[@]}"
create_install_list libs "${files[@]}"

ARCH="${isa}"
build_package "${argobots_lib}" "${libs[@]}"
build_debug_package "${argobots_lib}" "${dbg[@]}"

TARGET_PATH="${libdir}/pkgconfig"
list_files files "${SL_ARGOBOTS_PREFIX}/lib64/pkgconfig/*"
replace_paths "${SL_ARGOBOTS_PREFIX}" "${files[@]}"
create_install_list pkgcfg "${files[@]}"

TARGET_PATH="${libdir}"
list_files files "${SL_ARGOBOTS_PREFIX}/lib64/libabt.so"
create_install_list libs "${files[@]}"

TARGET_PATH="${includedir}"
list_files files "${SL_ARGOBOTS_PREFIX}/include/abt.h"
create_install_list headers "${files[@]}"

DEPENDS=("${argobots_lib}")
build_package "${argobots_devel}" \
  "${libs[@]}" \
  "${pkgcfg[@]}" \
  "${headers[@]}"
