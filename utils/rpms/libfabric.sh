#!/bin/bash
# (C) Copyright 2025 Google LLC
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_OFI_PREFIX}" ]; then
  echo "Libfabric must be installed or never built"
  exit 0
fi

bins=()
dbg_bin=()
dbg_lib=()
files=()
includes=()
internal_includes=()
libs=()
mans=()
pkgcfgs=()

VERSION="1.22.0"
RELEASE="3"
LICENSE="BSD or GPLv2"
ARCH=${isa}
DESCRIPTION="Provides a user-space API to access high-performance fabric
services, such as RDMA. This package contains the runtime library."
URL="https://github.com/ofiwg/libfabric"

if [[ "${DISTRO:-el8}" =~ "suse" ]]; then
  libfabric_name="libfabric1"
else
  libfabric_name="libfabric"
fi

TARGET_PATH="${bindir}"
list_files files "${SL_OFI_PREFIX}/bin/fi_*"
clean_bin dbg_bin "${files[@]}"
create_install_list bins "${files[@]}"

TARGET_PATH="${libdir}"
list_files files "${SL_OFI_PREFIX}/lib64/libfabric*.so.*"
clean_bin dbg_lib "${files[@]}"
create_install_list libs "${files[@]}"

TARGET_PATH="${mandir}/man1"
list_files files "${SL_OFI_PREFIX}/share/man/man1/fi_*.1*"
create_install_list mans "${files[@]}"

ARCH="${isa}"
build_package "${libfabric_name}" "${bins[@]}" "${mans[@]}" "${libs[@]}"
build_debug_package "${libfabric_name}" "${dbg_lib[@]}" "${dbg_bin[@]}"

TARGET_PATH="${libdir}"
list_files files "${SL_OFI_PREFIX}/lib64/libfabric*.so"
create_install_list libs "${files[@]}"

TARGET_PATH="${includedir}/rdma"
list_files files "${SL_OFI_PREFIX}/include/rdma/*.h"
create_install_list includes "${files[@]}"

TARGET_PATH="${includedir}/rdma/providers"
list_files files "${SL_OFI_PREFIX}/include/rdma/providers/*.h"
create_install_list internal_includes "${files[@]}"

TARGET_PATH="${libdir/pkgconfig}"
list_files files "${SL_OFI_PREFIX}/lib64/pkgconfig/${libfabric_name}.pc"
replace_paths "${SL_OFI_PREFIX}" "${files[@]}"
create_install_list pkgcfgs "${files[@]}"

man3=()
TARGET_PATH="${mandir}/man3"
list_files files "${SL_OFI_PREFIX}/share/man/man3/fi*.3*"
create_install_list man3 "${files[@]}"

man7=()
TARGET_PATH="${mandir}/man7"
list_files files "${SL_OFI_PREFIX}/share/man/man7/f*.7*"
create_install_list man7 "${files[@]}"

DEPENDS=("${libfabric_name}")
build_package "${libfabric_name}-devel" \
  "${libs[@]}" "${includes[@]}" "${man3[@]}" "${man7[@]}" "${pkgcfgs[@]}" "${internal_includes[@]}"
