#!/bin/bash
# (C) Copyright 2025 Google LLC
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_FUSED_PREFIX}" ]; then
  echo "fused must be installed or was never built"
  exit 0
fi

VERSION=${fused_version}
RELEASE=${fused_release}
LICENSE="BSD"
ARCH=${isa}
DESCRIPTION="DAOS version of libfuse"
URL="https://github.com/daos-stack/fused.git"

files=()
TARGET_PATH="${includedir}/fused"
list_files files "${SL_FUSED_PREFIX}/include/fused/*.h"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}"
list_files files "${SL_FUSED_PREFIX}/${libdir///usr/}/*.a"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}/pkgconfig"
list_files files "${SL_FUSED_PREFIX}/${libdir///usr/}/pkgconfig/fused.pc"
append_install_list "${files[@]}"

build_package "${fused_dev}"
