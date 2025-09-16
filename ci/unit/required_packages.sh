#!/bin/bash

set -eu

# No longer used but provided by pipeline-lib
# distro="$1"
# quick_build="${2:-false}"

OPENMPI_VER=""
PY_MINOR_VER=""

export DISTRO="el8" # should also work for el9
pkgs="$(utils/rpms/package_version.sh argobots lib)                  \
      boost-python3$PY_MINOR_VER-devel                               \
      capstone                                                       \
      $(utils/rpms/package_version.sh argobots lib)                  \
      $(utils/rpms/package_version.sh argobots debug)                \
      $(utils/rpms/package_version.sh daos_spdk dev)                 \
      $(utils/rpms/package_version.sh daos_spdk debug)               \
      $(utils/rpms/package_version.sh isal dev)                      \
      $(utils/rpms/package_version.sh isal debug)                    \
      $(utils/rpms/package_version.sh isal_crypto lib)               \
      $(utils/rpms/package_version.sh isal_crypto debug)             \
      $(utils/rpms/package_version.sh libfabric dev)                 \
      $(utils/rpms/package_version.sh libfabric debug)               \
      $(utils/rpms/package_version.sh mercury dev)                   \
      $(utils/rpms/package_version.sh mercury debug)                 \
      $(utils/rpms/package_version.sh pmdk lib pmemobj)              \
      $(utils/rpms/package_version.sh pmdk debug pmemobj)            \
      $(utils/rpms/package_version.sh pmdk debug pmem)               \
      fuse3                                                          \
      gotestsum                                                      \
      hwloc-devel                                                    \
      libasan                                                        \
      libipmctl-devel                                                \
      libyaml-devel                                                  \
      numactl                                                        \
      numactl-devel                                                  \
      openmpi$OPENMPI_VER                                            \
      patchelf                                                       \
      pciutils-devel                                                 \
      protobuf-c                                                     \
      valgrind-devel"

# output with trailing newline suppressed
echo  -e "$pkgs\c"
exit 0
