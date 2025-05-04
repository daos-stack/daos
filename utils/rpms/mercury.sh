#!/bin/bash
# (C) Copyright 2025 Google LLC
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_MERCURY_PREFIX}" ]; then
  echo "Mercury must be installed or never built"
  exit 0
fi

VERSION="${mercury_version}"
RELEASE="2"
LICENSE="BSD"
ARCH=${isa}
DESCRIPTION="Mercury is a Remote Procedure Call (RPC) framework specifically
designed for use in High-Performance Computing (HPC) systems with
high-performance fabrics. Its network implementation is abstracted
to make efficient use of native transports and allow easy porting
to a variety of systems. Mercury supports asynchronous transfer of
parameters and execution requests, and has dedicated support for
large data arguments that are transferred using Remote Memory
Access (RMA). Its interface is generic and allows any function
call to be serialized. Since code generation is done using the C
preprocessor, no external tool is required."
URL="http://mercury-hpc.github.io"

files=()
TARGET_PATH="${bindir}"
list_files files "${SL_MERCURY_PREFIX}/bin/*"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}"
list_files files "${SL_MERCURY_PREFIX}/lib64/lib*.so.*"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}/mercury"
list_files files "${SL_MERCURY_PREFIX}/lib64/mercury/libna_plugin_ofi.so"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

ARCH="${isa}"
DEPENDS=("${libfabric_lib} >= ${libfabric_version}")
build_package "mercury"
DEPENDS=()

TARGET_PATH="${libdir}/mercury"
list_files files "${SL_MERCURY_PREFIX}/lib64/mercury/libna_plugin_ucx.so"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

ARCH="${isa}"
build_package "mercury-ucx"

if [ "${BUILD_EXTRANEOUS:-no}" = "yes" ]; then
TARGET_PATH="${libdir}"
  list_files files "${SL_MERCURY_PREFIX}/lib64/lib*.so"
  append_install_list "${files[@]}"

  TARGET_PATH="${includedir}"
  list_files files "${SL_MERCURY_PREFIX}/include/*.h"
  append_install_list "${files[@]}"

  TARGET_PATH="${libdir/pkgconfig}"
  list_files files "${SL_MERCURY_PREFIX}/lib64/pkgconfig/*.pc"
  replace_paths "${SL_MERCURY_PREFIX}" "${files[@]}"
  if [ -n "${SL_OFI_PREFIX}" ]; then
    replace_paths "${SL_OFI_PREFIX}" "${files[@]}"
  fi
  if [ -n "${SL_UCX_PREFIX}" ]; then
    replace_paths "${SL_UCX_PREFIX}" "${files[@]}"
  fi
  append_install_list "${files[@]}"

  TARGET_PATH="${libdir}/cmake/mercury"
  list_files files "${SL_MERCURY_PREFIX}/lib64/cmake/mercury/*"
  replace_paths "${SL_MERCURY_PREFIX}" "${files[@]}"
  if [ -n "${SL_OFI_PREFIX}" ]; then
    replace_paths "${SL_OFI_PREFIX}" "${files[@]}"
  fi
  if [ -n "${SL_UCX_PREFIX}" ]; then
    replace_paths "${SL_UCX_PREFIX}" "${files[@]}"
  fi
  append_install_list "${files[@]}"

  DEPENDS=("mercury = ${mercury_version}")
  build_package "${mercury_dev}"
  DEPENDS=()
fi
