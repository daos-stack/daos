#!/bin/bash
# (C) Copyright 2025 Google LLC
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_MERCURY_PREFIX}" ]; then
  echo "Mercury must be installed or never built"
  exit 0
fi

bins=()
dbg_bin=()
dbg_lib=()
dbg_lib_internal=()
files=()
includes=()
libs=()
libs_internal=()
cmakes=()
pkgcfgs=()

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

TARGET_PATH="${bindir}"
list_files files "${SL_MERCURY_PREFIX}/bin/*"
clean_bin dbg_bin "${files[@]}"
create_install_list bins "${files[@]}"

TARGET_PATH="${libdir}"
list_files files "${SL_MERCURY_PREFIX}/lib64/lib*.so.*"
clean_bin dbg_lib "${files[@]}"
create_install_list libs "${files[@]}"

TARGET_PATH="${libdir}/mercury"
list_files files "${SL_MERCURY_PREFIX}/lib64/mercury/libna_plugin_ofi.so"
clean_bin dbg_lib_internal "${files[@]}"
create_install_list libs_internal "${files[@]}"

ARCH="${isa}"
DEPENDS=("${libfabric_lib} >= ${libfabric_version}")
build_package "mercury" "${bins[@]}" "${libs[@]}" "${libs_internal[@]}"
build_debug_package "mercury" "${dbg_lib[@]}" "${dbg_bin[@]}" "${dbg_lib_internal[@]}"
DEPENDS=()

TARGET_PATH="${libdir}/mercury"
list_files files "${SL_MERCURY_PREFIX}/lib64/mercury/libna_plugin_ucx.so"
clean_bin dbg_lib_internal "${files[@]}"
create_install_list libs_internal "${files[@]}"

ARCH="${isa}"
build_package "mercury-ucx" "${libs_internal[@]}"
build_debug_package "mercury-ucx" "${dbg_lib_internal[@]}"

TARGET_PATH="${libdir}"
list_files files "${SL_MERCURY_PREFIX}/lib64/lib*.so"
create_install_list libs "${files[@]}"

TARGET_PATH="${includedir}"
list_files files "${SL_MERCURY_PREFIX}/include/*.h"
create_install_list includes "${files[@]}"

TARGET_PATH="${libdir/pkgconfig}"
list_files files "${SL_MERCURY_PREFIX}/lib64/pkgconfig/*.pc"
replace_paths "${SL_MERCURY_PREFIX}" "${files[@]}"
if [ -n "${SL_OFI_PREFIX}" ]; then
  replace_paths "${SL_OFI_PREFIX}" "${files[@]}"
fi
if [ -n "${SL_UCX_PREFIX}" ]; then
  replace_paths "${SL_UCX_PREFIX}" "${files[@]}"
fi
create_install_list pkgcfgs "${files[@]}"

TARGET_PATH="${libdir}/cmake/mercury"
list_files files "${SL_MERCURY_PREFIX}/lib64/cmake/mercury/*"
replace_paths "${SL_MERCURY_PREFIX}" "${files[@]}"
if [ -n "${SL_OFI_PREFIX}" ]; then
  replace_paths "${SL_OFI_PREFIX}" "${files[@]}"
fi
if [ -n "${SL_UCX_PREFIX}" ]; then
  replace_paths "${SL_UCX_PREFIX}" "${files[@]}"
fi
create_install_list cmakes "${files[@]}"

DEPENDS=("mercury = ${mercury_version}")
build_package "mercury-devel" \
  "${libs[@]}" "${includes[@]}" "${pkgcfgs[@]}" "${cmakes[@]}"
DEPENDS=()

