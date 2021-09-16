#!/bin/bash

set -eux

distro="$1"
quick_build="${2:-false}"

pkgs="gotestsum openmpi3                 \
      hwloc-devel argobots               \
      fuse3-libs fuse3                   \
      boost-python36-devel               \
      libisa-l-devel libpmem             \
      libpmemobj protobuf-c              \
      spdk-devel libfabric-devel         \
      pmix numactl-devel                 \
      libipmctl-devel python36-pyxattr   \
      python36-junit_xml                 \
      python36-tabulate numactl          \
      libyaml-devel                      \
      valgrind-devel patchelf"

if $quick_build; then
    read -r mercury_version < "$distro"-required-mercury-rpm-version
    pkgs="$pkgs spdk-tools mercury-$mercury_version         \
          libisa-l_crypto libfabric-debuginfo   \
          argobots-debuginfo protobuf-c-debuginfo"
fi

echo "$pkgs"
exit 0
