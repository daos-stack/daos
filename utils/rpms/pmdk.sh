#!/bin/bash
# (C) Copyright 2025 Google LLC
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_PMDK_PREFIX}" ]; then
  echo "pmdk must be installed or never built"
  exit 0
fi

bins=()
dbg=()
files=()
includes=()
internal_includes=()
libs=()
mans=()
pkgcfgs=()
data=()

VERSION="2.1.0"
RELEASE="4"
LICENSE="BSD-3-Clause"
ARCH=${isa}
DESCRIPTION="The Persistent Memory Development Kit is a collection of libraries for
using memory-mapped persistence, optimized specifically for persistent memory."
URL="https://github.com/pmem/pmdk"

if [[ "${DISTRO:-el8}" =~ "suse" ]]; then
  LIBMAJOR=1
fi

# libpmem
TARGET_PATH="${libdir}"
list_files files "${SL_PMDK_PREFIX}/lib64/libpmem.so.*"
clean_bin dbg "${files[@]}"
create_install_list libs "${files[@]}"

TARGET_PATH="${datadir}/pmdk"
list_files files "${SL_PMDK_PREFIX}/share/pmdk/pmdk.magic"
create_install_list data "${files[@]}"

ARCH="${isa}"
build_package "libpmem${LIBMAJOR:-}" "${libs[@]}" "${data[@]}"
build_debug_package "libpmem${LIBMAJOR:-}" "${dbg[@]}"

#libpmemobj
DEPENDS=("libpmem${LIBMAJOR:-}")
TARGET_PATH="${libdir}"
list_files files "${SL_PMDK_PREFIX}/lib64/libpmemobj.so.*"
clean_bin dbg "${files[@]}"
create_install_list libs "${files[@]}"

ARCH="${isa}"
build_package "libpmemobj${LIBMAJOR:-}" "${libs[@]}"
build_debug_package "libpmemobj${LIBMAJOR:-}" "${dbg[@]}"

#libpmempool
TARGET_PATH="${libdir}"
list_files files "${SL_PMDK_PREFIX}/lib64/libpmempool.so.*"
clean_bin dbg "${files[@]}"
create_install_list libs "${files[@]}"

ARCH="${isa}"
build_package "libpmempool${LIBMAJOR:-}" "${libs[@]}"
build_debug_package "libpmempool${LIBMAJOR:-}" "${dbg[@]}"

#libpmem-devel
TARGET_PATH="${libdir}"
list_files files "${SL_PMDK_PREFIX}/lib64/libpmem.so"
create_install_list libs "${files[@]}"

TARGET_PATH="${libdir/pkgconfig}"
list_files files "${SL_PMDK_PREFIX}/lib64/pkgconfig/libpmem.pc"
replace_paths "${SL_PMDK_PREFIX}" "${files[@]}"
create_install_list pkgcfgs "${files[@]}"

TARGET_PATH="${includedir}"
list_files files "${SL_PMDK_PREFIX}/include/libpmem.h"
create_install_list includes "${files[@]}"

man7=()
TARGET_PATH="${mandir}/man7"
list_files files "${SL_PMDK_PREFIX}/share/man/man7/libpmem.7.gz"
create_install_list man7 "${files[@]}"

man5=()
TARGET_PATH="${mandir}/man5"
list_files files "${SL_PMDK_PREFIX}/share/man/man5/pmem_ctl.5.gz"
create_install_list man5 "${files[@]}"

man3=()
TARGET_PATH="${mandir}/man3"
list_files files "${SL_PMDK_PREFIX}/share/man/man3/pmem_*.3.gz"
create_install_list man3 "${files[@]}"

build_package "libpmem-devel" \
  "${libs[@]}" "${pkgcfgs[@]}" "${includes[@]}" "${man7[@]}" "${man5[@]}" "${man3[@]}"

#libpmemobj-devel
TARGET_PATH="${libdir}"
list_files files "${SL_PMDK_PREFIX}/lib64/libpmemobj.so"
create_install_list libs "${files[@]}"

TARGET_PATH="${libdir/pkgconfig}"
list_files files "${SL_PMDK_PREFIX}/lib64/pkgconfig/libpmemobj.pc"
replace_paths "${SL_PMDK_PREFIX}" "${files[@]}"
create_install_list pkgcfgs "${files[@]}"

TARGET_PATH="${includedir}"
list_files files "${SL_PMDK_PREFIX}/include/libpmemobj.h"
create_install_list includes "${files[@]}"

TARGET_PATH="${includedir}/libpmemobj"
list_files files "${SL_PMDK_PREFIX}/include/libpmemobj/*.h"
create_install_list internal_includes "${files[@]}"

TARGET_PATH="${mandir}/man7"
list_files files "${SL_PMDK_PREFIX}/share/man/man7/libpmemobj.7.gz"
create_install_list man7 "${files[@]}"

man5=()
TARGET_PATH="${mandir}/man5"
list_files files "${SL_PMDK_PREFIX}/share/man/man5/poolset.5.gz"
create_install_list man5 "${files[@]}"

man3=()
TARGET_PATH="${mandir}/man3"
list_files files "${SL_PMDK_PREFIX}/share/man/man3/pmemobj_*.3.gz" \
  "${SL_PMDK_PREFIX}/share/man/man3/pobj_*.3.gz" \
  "${SL_PMDK_PREFIX}/share/man/man3/oid_*.3.gz" \
  "${SL_PMDK_PREFIX}/share/man/man3/toid_*.3.gz" \
  "${SL_PMDK_PREFIX}/share/man/man3/direct_*.3.gz" \
  "${SL_PMDK_PREFIX}/share/man/man3/d_r*.3.gz" \
  "${SL_PMDK_PREFIX}/share/man/man3/tx_*.3.gz"
create_install_list man3 "${files[@]}"

DEPENDS=("libpmem-devel" "libpmemobj${LIBMAJOR:-}")
build_package "libpmemobj-devel" \
  "${libs[@]}" "${pkgcfgs[@]}" "${includes[@]}" "${internal_includes[@]}" "${man7[@]}" \
  "${man5[@]}" "${man3[@]}"

#libpmempool-devel
TARGET_PATH="${libdir}"
list_files files "${SL_PMDK_PREFIX}/lib64/libpmempool.so"
create_install_list libs "${files[@]}"

TARGET_PATH="${libdir/pkgconfig}"
list_files files "${SL_PMDK_PREFIX}/lib64/pkgconfig/libpmempool.pc"
replace_paths "${SL_PMDK_PREFIX}" "${files[@]}"
create_install_list pkgcfgs "${files[@]}"

TARGET_PATH="${includedir}"
list_files files "${SL_PMDK_PREFIX}/include/libpmempool.h"
create_install_list includes "${files[@]}"

TARGET_PATH="${mandir}/man7"
list_files files "${SL_PMDK_PREFIX}/share/man/man7/libpmempool.7.gz"
create_install_list man7 "${files[@]}"

TARGET_PATH="${mandir}/man5"
list_files files "${SL_PMDK_PREFIX}/share/man/man5/poolset.5.gz"
create_install_list man5 "${files[@]}"

TARGET_PATH="${mandir}/man3"
list_files files "${SL_PMDK_PREFIX}/share/man/man3/pmempool_*.3.gz"
create_install_list man3 "${files[@]}"

DEPENDS=("libpmem-devel" "libpmempool${LIBMAJOR:-}")
build_package "libpmemobj-devel" \
  "${libs[@]}" "${pkgcfgs[@]}" "${includes[@]}" "${man7[@]}" "${man5[@]}" "${man3[@]}"

#pmempool
TARGET_PATH="${bindir}"
list_files files "${SL_PMDK_PREFIX}/bin/pmempool"
clean_bin dbg "${files[@]}"
create_install_list bins "${files[@]}"

TARGET_PATH="${mandir}/man1"
list_files files "${SL_PMDK_PREFIX}/share/man/man1/pmempool.1.gz" \
  "${SL_PMDK_PREFIX}/share/man/man1/pmempool-*.1.gz"
create_install_list mans "${files[@]}"

DEPENDS=("libpmem${LIBMAJOR:-}" "libpmempool${LIBMAJOR:-}" "libpmemobj${LIBMAJOR:-}")
build_package "pmempool" "${bins[@]}" "${mans[@]}"

#pmreorder
TARGET_PATH="${bindir}"
list_files files "${SL_PMDK_PREFIX}/bin/pmreorder"
replace_paths "${SL_PMDK_PREFIX}" "${files[@]}"
create_install_list bins "${files[@]}"

TARGET_PATH="${mandir}/man1"
list_files files "${SL_PMDK_PREFIX}/share/man/man1/pmreorder.1.gz"
create_install_list mans "${files[@]}"

TARGET_PATH="${datadir}/pmreorder"
list_files files "${SL_PMDK_PREFIX}/share/pmreorder/*.py"
create_install_list data "${files[@]}"

DEPENDS=()
build_package "pmreorder" "${bins[@]}" "${mans[@]}" "${data[@]}"

#daxio
TARGET_PATH="${bindir}"
list_files files "${SL_PMDK_PREFIX}/bin/daxio"
clean_bin dbg "${files[@]}"
create_install_list bins "${files[@]}"

TARGET_PATH="${mandir}/man1"
list_files files "${SL_PMDK_PREFIX}/share/man/man1/daxio.1.gz"
create_install_list mans "${files[@]}"

DEPENDS=("libpmem${LIBMAJOR:-}")
build_package "daxio" "${bins[@]}" "${mans[@]}"
