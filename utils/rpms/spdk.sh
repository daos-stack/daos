#!/bin/bash
# (C) Copyright 2025 Google LLC
set -eEuo pipefail
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_SPDK_PREFIX:-}" ]; then
  echo "spdk must be installed or never built"
  exit 0
fi

VERSION=${daos_spdk_version}
RELEASE=${daos_spdk_release}
LICENSE="BSD"
ARCH=${isa}
DESCRIPTION="The Storage Performance Development Kit provides a set of tools
and libraries for writing high performance, scalable, user-mode storage
applications."
URL="https://spdk.io"
RPM_CHANGELOG="spdk.changelog"

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

TARGET_PATH="${libdir}/daos_srv/dpdk/pmds-24.1"
list_files files "${SL_SPDK_PREFIX}/lib64/daos_srv/dpdk/pmds-24.1/lib*.so.*"
clean_bin "${files[@]}"
append_install_list "${files[@]}"

mkdir -p "${tmp}/${sysconfdir}/ld.so.conf.d"
echo "${libdir}/daos_srv" > "${tmp}/${sysconfdir}/ld.so.conf.d/daos.conf"
install_list+=("${tmp}/${sysconfdir}/ld.so.conf.d/daos.conf=${sysconfdir}/ld.so.conf.d/daos.conf")

cat << EOF  > "${tmp}/post_install_server"
#!/bin/bash
set -x
ldconfig
EOF
  EXTRA_OPTS+=("--after-install" "${tmp}/post_install_server")

ARCH="${isa}"
CONFLICTS=("spdk" "dpdk")
build_package "daos-spdk"

TARGET_PATH="${libdir}/daos_srv"
list_files files "${SL_SPDK_PREFIX}/lib64/daos_srv/libspdk*.so" \
  "${SL_SPDK_PREFIX}/lib64/daos_srv/librte*.so"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}/pkgconfig"
list_files files "${SL_SPDK_PREFIX}/lib64/pkgconfig/daos_spdk.pc"
replace_paths "${SL_SPDK_PREFIX}" "${files[@]}"
append_install_list "${files[@]}"

TARGET_PATH="${libdir}/daos_srv/dpdk/pmds-24.1"
list_files files "${SL_SPDK_PREFIX}/lib64/daos_srv/dpdk/pmds-24.1/lib*.so"
append_install_list "${files[@]}"

TARGET_PATH="${includedir}/daos_srv/spdk"
list_files files "${SL_SPDK_PREFIX}/include/daos_srv/spdk/*.h"
append_install_list "${files[@]}"

TARGET_PATH="${includedir}/daos_srv/dpdk/generic"
list_files files "${SL_SPDK_PREFIX}/include/daos_srv/dpdk/generic/*.h"
append_install_list "${files[@]}"

TARGET_PATH="${includedir}/daos_srv/dpdk"
list_files files "${SL_SPDK_PREFIX}/include/daos_srv/dpdk/*.h"
append_install_list "${files[@]}"

DEPENDS=("daos-spdk = ${daos_spdk_full}")
build_package "daos-spdk-devel"
RPM_CHANGELOG=
