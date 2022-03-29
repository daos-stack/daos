#!/bin/bash

set -eux

distro="$1"
quick_build="${2:-false}"

if [[ "$distro" = *7 ]]; then
    OPENMPI_VER="3"
    PY_MINOR_VER="6"
elif [[ "$distro" = *8 ]]; then
    OPENMPI_VER=""
    PY_MINOR_VER=""
fi
pkgs="gotestsum openmpi$OPENMPI_VER                \
      hwloc-devel argobots                         \
      fuse3-libs fuse3                             \
      boost-python3$PY_MINOR_VER-devel             \
      libisa-l-devel libpmem                       \
      libpmemobj protobuf-c                        \
      spdk-devel libfabric-devel                   \
      pmix numactl-devel                           \
      libipmctl-devel python3$PY_MINOR_VER-pyxattr \
      python3$PY_MINOR_VER-junit_xml               \
      python3$PY_MINOR_VER-tabulate numactl        \
      libyaml-devel                                \
      valgrind-devel patchelf"

if $quick_build; then
    if ! read -r mercury_version < "$distro"-required-mercury-rpm-version; then
        echo "Error reading from $distro-required-mercury-rpm-version"
        if ! ls -l "$distro"-required-mercury-rpm-version; then
            ls -l
        fi
        cat "$distro"-required-mercury-rpm-version
        exit 1
    fi
    pkgs="$pkgs spdk-tools mercury\ \>=\ $mercury_version \
          libisa-l_crypto libfabric-debuginfo             \
          argobots-debuginfo protobuf-c-debuginfo"
fi

echo "$pkgs"
exit 0
