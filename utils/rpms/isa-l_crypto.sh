#!/bin/bash
# (C) Copyright 2025 Google LLC
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_ISAL_CRYPTO_PREFIX}" ]; then
  echo "isa-l_crypto must be installed or never built"
  exit 0
fi

VERSION="${isal_crypto_version}"
RELEASE="2"
LICENSE="BSD-3-Clause"
ARCH=${isa}
DESCRIPTION="ISA-L_crypto is a collection of optimized low-level functions
targeting storage applications. ISA-L_crypto includes:
- Multi-buffer hashes - run multiple hash jobs together on one core
for much better throughput than single-buffer versions. (
SHA1, SHA256, SHA512, MD5)
- Multi-hash - Get the performance of multi-buffer hashing with a
  single-buffer interface.
- Multi-hash + murmur - run both together.
- AES - block ciphers (XTS, GCM, CBC)
- Rolling hash - Hash input in a window which moves through the input
Provides various algorithms for erasure coding, crc, raid, compression and
decompression"
URL="https://github.com/intel/isa-l_crypto"

files=()
TARGET_PATH="${libdir}"
list_files files "${SL_ISAL_CRYPTO_PREFIX}/lib64/libisal_crypto.so.*"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

build_package "${isal_crypto_lib}"

if [ "${BUILD_EXTRANEOUS:-no}" = "yes" ]; then
  TARGET_PATH="${libdir}"
  list_files files "${SL_ISAL_CRYPTO_PREFIX}/lib64/libisal_crypto.so"
  append_install_list "${files[@]}"

  TARGET_PATH="${libdir/pkgconfig}"
  list_files files "${SL_ISAL_CRYPTO_PREFIX}/lib64/pkgconfig/libisal_crypto.pc"
  replace_paths "${SL_ISAL_CRYPTO_PREFIX}" "${files[@]}"
  append_install_list "${files[@]}"

  TARGET_PATH="${includedir}"
  list_files files "${SL_ISAL_CRYPTO_PREFIX}/include/isa-l_crypto.h"
  append_install_list "${files[@]}"

  TARGET_PATH="${includedir}/isa-l_crypto"
  list_files files "${SL_ISAL_CRYPTO_PREFIX}/include/isa-l_crypto/*"
  append_install_list "${files[@]}"

  DEPENDS=("${isal_crypto_lib} = ${isal_crypto_version}")
  build_package "${isal_crypto_dev}"
fi
