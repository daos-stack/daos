#!/bin/bash
# (C) Copyright 2025 Google LLC
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_PMDK_PREFIX:-}" ]; then
  echo "pmdk must be installed or never built"
  exit 0
fi

VERSION="${pmdk_version}"
RELEASE="${pmdk_release}"
LICENSE="BSD-3-Clause"
ARCH=${isa}
DESCRIPTION="The Persistent Memory Development Kit is a collection of libraries for
using memory-mapped persistence, optimized specifically for persistent memory."
URL="https://github.com/pmem/pmdk"
RPM_CHANGELOG="pmdk.changelog"

files=()

# libpmem
TARGET_PATH="${libdir}"
list_files files "${SL_PMDK_PREFIX}/lib64/libpmem.so.*"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${datadir}/pmdk"
list_files files "${SL_PMDK_PREFIX}/share/pmdk/pmdk.magic"
append_install_list "${files[@]}"

ARCH="${isa}"
build_package "${pmem_lib}"

#libpmemobj
DEPENDS=("${pmem_lib}")
TARGET_PATH="${libdir}"
list_files files "${SL_PMDK_PREFIX}/lib64/libpmemobj.so.*"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

ARCH="${isa}"
build_package "${pmemobj_lib}"

#libpmem-devel
TARGET_PATH="${libdir}"
list_files files "${SL_PMDK_PREFIX}/lib64/libpmem.so"
append_install_list "${files[@]}"

TARGET_PATH="${libdir/pkgconfig}"
list_files files "${SL_PMDK_PREFIX}/lib64/pkgconfig/libpmem.pc"
replace_paths "${SL_PMDK_PREFIX}" "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${includedir}"
list_files files "${SL_PMDK_PREFIX}/include/libpmem.h"
append_install_list "${files[@]}"

TARGET_PATH="${mandir}/man7"
list_files files "${SL_PMDK_PREFIX}/share/man/man7/libpmem.7.gz"
append_install_list "${files[@]}"

TARGET_PATH="${mandir}/man5"
list_files files "${SL_PMDK_PREFIX}/share/man/man5/pmem_ctl.5.gz"
append_install_list "${files[@]}"

TARGET_PATH="${mandir}/man3"
list_files files "${SL_PMDK_PREFIX}/share/man/man3/pmem_*.3.gz"
append_install_list "${files[@]}"

build_package "libpmem-devel"

#libpmemobj-devel
TARGET_PATH="${libdir}"
list_files files "${SL_PMDK_PREFIX}/lib64/libpmemobj.so"
append_install_list "${files[@]}"

TARGET_PATH="${libdir/pkgconfig}"
list_files files "${SL_PMDK_PREFIX}/lib64/pkgconfig/libpmemobj.pc"
replace_paths "${SL_PMDK_PREFIX}" "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${includedir}"
list_files files "${SL_PMDK_PREFIX}/include/libpmemobj.h"
append_install_list "${files[@]}"

TARGET_PATH="${includedir}/libpmemobj"
list_files files "${SL_PMDK_PREFIX}/include/libpmemobj/*.h"
append_install_list "${files[@]}"

TARGET_PATH="${mandir}/man7"
list_files files "${SL_PMDK_PREFIX}/share/man/man7/libpmemobj.7.gz"
append_install_list "${files[@]}"

TARGET_PATH="${mandir}/man5"
list_files files "${SL_PMDK_PREFIX}/share/man/man5/poolset.5.gz"
append_install_list "${files[@]}"

TARGET_PATH="${mandir}/man3"
list_files files "${SL_PMDK_PREFIX}/share/man/man3/pmemobj_*.3.gz" \
  "${SL_PMDK_PREFIX}/share/man/man3/pobj_*.3.gz" \
  "${SL_PMDK_PREFIX}/share/man/man3/oid_*.3.gz" \
  "${SL_PMDK_PREFIX}/share/man/man3/toid_*.3.gz" \
  "${SL_PMDK_PREFIX}/share/man/man3/direct_*.3.gz" \
  "${SL_PMDK_PREFIX}/share/man/man3/d_r*.3.gz" \
  "${SL_PMDK_PREFIX}/share/man/man3/tx_*.3.gz"
append_install_list "${files[@]}"

DEPENDS=("${pmem_dev} = ${pmdk_full}" "${pmemobj_lib} = ${pmdk_full}")
build_package "${pmemobj_dev}"

if [ "${BUILD_EXTRANEOUS:-no}" = "yes" ]; then
  #libpmempool
  TARGET_PATH="${libdir}"
  list_files files "${SL_PMDK_PREFIX}/lib64/libpmempool.so.*"
  clean_bin "${files[@]}"
  append_install_list "${files[@]}"

  ARCH="${isa}"
  build_package "${pmempool_lib}"

  #pmempool
  TARGET_PATH="${bindir}"
  list_files files "${SL_PMDK_PREFIX}/bin/pmempool"
  clean_bin "${files[@]}"
  append_install_list "${files[@]}"

  TARGET_PATH="${mandir}/man1"
  list_files files "${SL_PMDK_PREFIX}/share/man/man1/pmempool.1.gz" \
    "${SL_PMDK_PREFIX}/share/man/man1/pmempool-*.1.gz"
  append_install_list "${files[@]}"

  DEPENDS=("${pmem_lib} = ${pmdk_full}" "${pmemobj_lib} = ${pmdk_full}")
  DEPENDS+=("${pmempool_lib} = ${pmdk_full}")
  build_package "pmempool"

  #pmreorder
  TARGET_PATH="${bindir}"
  list_files files "${SL_PMDK_PREFIX}/bin/pmreorder"
  replace_paths "${SL_PMDK_PREFIX}" "${files[@]}"
  append_install_list "${files[@]}"

  TARGET_PATH="${mandir}/man1"
  list_files files "${SL_PMDK_PREFIX}/share/man/man1/pmreorder.1.gz"
  append_install_list "${files[@]}"

  TARGET_PATH="${datadir}/pmreorder"
  list_files files "${SL_PMDK_PREFIX}/share/pmreorder/*.py"
  append_install_list "${files[@]}"

  DEPENDS=()
  build_package "pmreorder"

  #daxio
  TARGET_PATH="${bindir}"
  list_files files "${SL_PMDK_PREFIX}/bin/daxio"
  clean_bin "${files[@]}"
  append_install_list "${files[@]}"

  TARGET_PATH="${mandir}/man1"
  list_files files "${SL_PMDK_PREFIX}/share/man/man1/daxio.1.gz"
  append_install_list "${files[@]}"

  DEPENDS=("${pmem_lib} = ${pmdk_full}")
  build_package "daxio"
fi
RPM_CHANGELOG=
