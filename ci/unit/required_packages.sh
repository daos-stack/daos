#!/bin/bash

set -eux

distro="$1"
# No longer used but provided by pipeline-lib
# quick_build="${2:-false}"

OPENMPI_VER=""
PY_MINOR_VER=""

pkgs="argobots            \
      capstone            \
      fuse3               \
      hwloc-devel         \
      libfabric-devel     \
      libyaml-devel       \
      numactl             \
      openmpi$OPENMPI_VER \
      patchelf            \
      pciutils-devel      \
      pmix                \
      protobuf-c          \
      spdk-devel          \
      valgrind-devel"

if [[ $distro = el* ]]; then
    pkgs+=" boost-python3$PY_MINOR_VER-devel \
            gotestsum                        \
            libipmctl-devel                  \
            libisa-l-devel                   \
            libpmem                          \
            libpmemobj                       \
            numactl-devel"
elif [[ $distro = leap* ]]; then
    # need https://artifactory.dc.hpdd.intel.com/ui/repos/tree/General/opensuse-proxy%2Frepositories%2Fdevel:%2Flanguages:%2Fgo to install gotestsum
    pkgs+=" boost-devel  \
            ipmctl-devel \
            isa-l-devel  \
            libpmem1     \
            libpmemobj1  \
            libnuma-devel"
fi

# output with trailing newline suppressed
echo  -e "$pkgs\c"
exit 0
