#!/bin/bash
root="$(realpath "$(dirname "$(dirname "$(dirname "${BASH_SOURCE[0]}")")")")"
set_lib_name() {
  comp="$1"; shift
  vartype="$1"; shift
  el_lib="$1"; shift
  suse_lib="$1"; shift
  deb_lib="$1"; shift
  local extension=""
  local dist="${DISTRO:-el8}"
  local -n _lib="${comp}_${vartype}"
  if [ "${vartype}" = "dev" ]; then
    if [[ "${dist}" =~ suse|el ]]; then
      extension="-devel"
    else
      extension="-dev"
    fi

  fi

  if [[ "${dist}" =~ suse ]]; then
    _lib="${suse_lib}${extension}"
  elif [[ "${dist}" =~ el ]]; then
    _lib="${el_lib}${extension}"
  else
    _lib="${deb_lib}${extension}"
  fi
}

if [[ "${DISTRO:-el8}" =~ suse ]]; then
  # Refine this later
  distro_name=".lp155"
else
  distro_name=".${DISTRO:-el8}"
fi

daos_version="$(grep "^Version: " "${root}/utils/rpms/daos.spec" | sed 's/^Version: *//')"
export daos_version
daos_release="$(grep "^Release: " "${root}/utils/rpms/daos.spec" | \
  sed 's/^Release: *//' | sed 's/%.*//')${DAOS_RELVAL:-}${distro_name}"
export daos_release

export libfabric_version="1.22.0"
export libfabric_release="3${distro_name}"
export libfabric_full="${libfabric_version}-${libfabric_release}"
export mercury_version="2.4.0"
export mercury_release="6${distro_name}"
export mercury_full="${mercury_version}-${mercury_release}"
export argobots_version="1.2"
export argobots_release="2${distro_name}"
export argobots_full="${argobots_version}-${argobots_release}"
export pmdk_version="2.1.0"
export pmdk_release="5${distro_name}"
export pmdk_full="${pmdk_version}-${pmdk_release}"
export isal_version="2.31.1"
export isal_release="6${distro_name}"
export isal_full="${isal_version}-${isal_release}"
export isal_crypto_version="2.24.0"
export isal_crypto_release="2${distro_name}"
export isal_crypto_full="${isal_crypto_version}-${isal_crypto_release}"
export daos_spdk_version="1.0.0"
export daos_spdk_release="2${distro_name}"
export daos_spdk_full="${daos_spdk_version}-${daos_spdk_release}"
export fused_version="1.0.0"
export fused_release="2${distro_name}"
export fused_full="${fused_version}-${fused_release}"

set_lib_name openmpi lib openmpi openmpi3 openmpi
export openmpi_lib
set_lib_name argobots lib argobots libabt0 libabt0
export argobots_lib
set_lib_name argobots dev argobots libabt libabt0
export argobots_dev

set_lib_name daos_spdk dev daos-spdk daos-spdk daos-spdk
export spdk_dev
set_lib_name daos_spdk lib daos-spdk daos-spdk daos-spdk
export spdk_lib

set_lib_name capstone lib capstone libcapstone4 libcapstone4
export capstone_lib

set_lib_name isal lib libisa-l libisal2 libisal2
export isal_lib
set_lib_name isal dev isa-l libisal libisal2
export isal_dev

set_lib_name isal_crypto lib libisa-l_crypto libisal_crypto2 libisal-crypto2
export isal_crypto_lib
set_lib_name isal_crypto dev isa-l_crypto libisal_crypto libisal-crypto2
export isal_crypto_dev

set_lib_name libfabric lib libfabric libfabric1 libfabric1
export libfabric_lib
set_lib_name libfabric dev libfabric libfabric libfabric
export libfabric_dev

set_lib_name mercury dev mercury mercury mercury
export mercury_dev
set_lib_name mercury lib mercury mercury mercury
export mercury_lib

set_lib_name pmemobj lib libpmemobj libpmemobj1 libpmemobj1
set_lib_name pmemobj dev libpmemobj libpmemobj1 libpmemobj1
set_lib_name pmem lib libpmem libpmem1 libpmem1
set_lib_name pmem dev libpmem libpmem libpmem1
set_lib_name pmempool lib libpmempool libpmempool1 libpmempool1
export pmem_lib
export pmem_dev
export pmemobj_lib
export pmemobj_dev
export pmempool_lib

set_lib_name fused dev fused fused fused
export fused_dev

set_lib_name protobufc lib protobuf-c libprotobuf-c1 libprotobuf-c1
export protobufc_lib

set_lib_name ndctl dev ndctl libndctl libndctl
export ndctl_dev

set_lib_name daos dev daos daos daos
export daos_dev

set_lib_name uuid lib libuuid libuuid1 libuuid1
export uuid_lib

set_lib_name hdf5 lib hdf5 hdf5 hdf5
export hdf5_lib

lmod="Lmod"
if [[ "${DISTRO:-el8}" =~ suse ]]; then
  lmod="lua-lmod"
fi
export lmod
