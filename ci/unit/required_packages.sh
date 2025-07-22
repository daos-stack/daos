#!/bin/bash

set -eux

# No longer used but provided by pipeline-lib
# distro="$1"
# quick_build="${2:-false}"

OPENMPI_VER=""
PY_MINOR_VER=""

pkgs="argobots                         \
      boost-python3$PY_MINOR_VER-devel \
      capstone                         \
      fuse3                            \
      fuse3-libs                       \
      gotestsum                        \
      hwloc-devel                      \
      libasan                          \
      libipmctl-devel                  \
      libisa-l-devel                   \
      libfabric-devel                  \
      libpmem                          \
      libpmemobj                       \
      libyaml-devel                    \
      numactl                          \
      numactl-devel                    \
      openmpi$OPENMPI_VER              \
      patchelf                         \
      pciutils-devel                   \
      pmix                             \
      protobuf-c                       \
      spdk-devel                       \
      valgrind-devel"

# output with trailing newline suppressed
echo  -e "$pkgs\c"
exit 0
