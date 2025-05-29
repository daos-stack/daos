#!/bin/bash
# (C) Copyright 2025 Google LLC
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_ISAL_PREFIX}" ]; then
  echo "isa-l must be installed or never built"
  exit 0
fi

VERSION="${isal_version}"
RELEASE="3"
LICENSE="BSD-3-Clause"
ARCH=${isa}
DESCRIPTION="Intelligent Storage Acceleration Library.
Provides various algorithms for erasure coding, crc, raid, compression and
decompression"
URL="https://github.com/intel/isa-l"

files=()
TARGET_PATH="${bindir}"
list_files files "${SL_ISAL_PREFIX}/bin/igzip"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${mandir}/man1"
list_files files "${SL_ISAL_PREFIX}/share/man/man1/igzip.*"
append_install_list "${files[@]}"

ARCH="${isa}"
build_package "isa-l"

TARGET_PATH="${libdir}"
list_files files "${SL_ISAL_PREFIX}/lib64/libisal.so.*"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

DEPENDS=("isa-l")
build_package "${isal_lib}"

if [ "${BUILD_EXTRANEOUS:-no}" = "yes" ]; then
  TARGET_PATH="${libdir}"
  list_files files "${SL_ISAL_PREFIX}/lib64/libisal.so"
  append_install_list "${files[@]}"

  TARGET_PATH="${libdir/pkgconfig}"
  list_files files "${SL_ISAL_PREFIX}/lib64/pkgconfig/libisal.pc"
  replace_paths "${SL_ISAL_PREFIX}" "${files[@]}"
  append_install_list "${files[@]}"

  TARGET_PATH="${includedir}"
  list_files files "${SL_ISAL_PREFIX}/include/isa-l.h"
  append_install_list "${files[@]}"

  TARGET_PATH="${includedir}/isa-l"
  list_files files "${SL_ISAL_PREFIX}/include/isa-l/*"
  append_install_list "${files[@]}"

  DEPENDS=("${isal_lib} = ${isal_version}")
  build_package "${isal_dev}"
fi
