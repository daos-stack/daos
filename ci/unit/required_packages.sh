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
pkgs="argobots                         \
      boost-python3$PY_MINOR_VER-devel \
      capstone                         \
      fuse3                            \
      fuse3-libs                       \
      gotestsum                        \
      hwloc-devel                      \
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
      pmix                             \
      protobuf-c                       \
      python3$PY_MINOR_VER-junit_xml   \
      python3$PY_MINOR_VER-pyxattr     \
      python3$PY_MINOR_VER-tabulate    \
      spdk-devel                       \
      valgrind-devel"

if $quick_build; then
    if ! read -r mercury_version < "$distro"-required-mercury-rpm-version; then
        echo "Error reading from $distro-required-mercury-rpm-version"
        if ! ls -l "$distro"-required-mercury-rpm-version; then
            ls -l
        fi
        cat "$distro"-required-mercury-rpm-version
        exit 1
    fi
    pkgs+=" spdk-tools mercury\ \>=\ $mercury_version"
    pkgs+=" libisa-l_crypto libfabric-debuginfo"
    pkgs+=" argobots-debuginfo"
    if [[ "$distro" == *7 ]];then
        pkgs+=" protobuf-c-debuginfo"
    fi
fi

# output with trailing newline suppressed
echo  -e "$pkgs\c"
exit 0
