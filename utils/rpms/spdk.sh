#!/bin/bash
# (C) Copyright 2025 Google LLC
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_SPDK_PREFIX}" ]; then
  echo "spdk never built"
  exit 1
fi

VERSION=${daos_version}
RELEASE=${daos_release}
LICENSE="BSD"
ARCH=${isa}
DESCRIPTION="The Storage Performance Development Kit provides a set of tools
and libraries for writing high performance, scalable, user-mode storage
applications."
URL="https://spdk.io"

files=()
TARGET_PATH="${bindir}"
list_files files "${SL_SPDK_PREFIX}/bin/daos_spdk*"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

BASE_PATH="${tmp}/${datadir}/daos/spdk"
TARGET_PATH="${datadir}/daos/spdk"
list_files files "${SL_SPDK_PREFIX}/share/daos/spdk/*"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}/daos_srv"
list_files files "${SL_SPDK_PREFIX}/lib64/daos_srv/libspdk*.so.*" \
  "${SL_SPDK_PREFIX}/lib64/daos_srv/librte*.so.*"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}/daos_srv/dpdk/pmds-22.0"
list_files files "${SL_SPDK_PREFIX}/lib64/daos_srv/dpdk/pmds-22.0/lib*.so.*"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

ARCH="${isa}"
build_package "daos-spdk"
