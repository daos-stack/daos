#!/bin/bash
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

set_lib_name argobots lib argobots libabt0 libabt0
export argobots_lib
set_lib_name argobots dev argobots libabt libabt0
export argobots_dev

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

set_lib_name pmemobj lib libpmemobj libpmemobj1 libpmemobj1
set_lib_name pmem lib libpmem libpmem1 libpmem1
set_lib_name pmempool lib libpmempool libpmempool1 libpmempool1
export pmem_lib
export pmemobj_lib
export pmempool_lib

set_lib_name protobufc lib protobuf-c libprotobuf-c1 libprotobuf-c1
export protobufc_lib

set_lib_name ndctl lib ndctl libndctl libndctl
export ndctl_lib

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
