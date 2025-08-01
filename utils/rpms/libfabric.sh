#!/bin/bash
# (C) Copyright 2025 Google LLC
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_OFI_PREFIX}" ]; then
  echo "Libfabric must be installed or never built"
  exit 0
fi

VERSION="${libfabric_version}"
RELEASE="3"
LICENSE="BSD or GPLv2"
ARCH=${isa}
DESCRIPTION="Provides a user-space API to access high-performance fabric
services, such as RDMA. This package contains the runtime library."
URL="https://github.com/ofiwg/libfabric"

files=()
TARGET_PATH="${bindir}"
list_files files "${SL_OFI_PREFIX}/bin/fi_*"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}"
list_files files "${SL_OFI_PREFIX}/lib64/libfabric*.so.*"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${mandir}/man1"
list_files files "${SL_OFI_PREFIX}/share/man/man1/fi_*.1*"
append_install_list "${files[@]}"

EXTRA_OPTS=()
cat << EOF  > "${tmp}/post_install_libfabric"
ldconfig
EOF
EXTRA_OPTS+=("--after-install" "${tmp}/post_install_libfabric")
EXTRA_OPTS+=("--rpm-autoprov")
ARCH="${isa}"
build_package "${libfabric_lib}"

TARGET_PATH="${libdir}"
list_files files "${SL_OFI_PREFIX}/lib64/libfabric*.so"
append_install_list "${files[@]}"

TARGET_PATH="${includedir}/rdma"
list_files files "${SL_OFI_PREFIX}/include/rdma/*.h"
append_install_list "${files[@]}"

TARGET_PATH="${includedir}/rdma/providers"
list_files files "${SL_OFI_PREFIX}/include/rdma/providers/*.h"
append_install_list "${files[@]}"

TARGET_PATH="${libdir/pkgconfig}"
list_files files "${SL_OFI_PREFIX}/lib64/pkgconfig/libfabric.pc"
replace_paths "${SL_OFI_PREFIX}" "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${mandir}/man3"
list_files files "${SL_OFI_PREFIX}/share/man/man3/fi*.3*"
append_install_list "${files[@]}"

TARGET_PATH="${mandir}/man7"
list_files files "${SL_OFI_PREFIX}/share/man/man7/f*.7*"
append_install_list "${files[@]}"

DEPENDS=("${libfabric_lib} = ${libfabric_version}")
build_package "${libfabric_dev}"
