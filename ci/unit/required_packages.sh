#!/bin/bash

set -eu

distro="${1:-el8}"
code_coverage="${2:-false}"

OPENMPI_VER=""
PY_MINOR_VER=""

export DISTRO="${distro}"
pkgs=("$(utils/rpms/package_version.sh argobots lib)")
pkgs+=("boost-python3${PY_MINOR_VER}-devel")
pkgs+=("capstone")
pkgs+=("$(utils/rpms/package_version.sh argobots lib)")
pkgs+=("$(utils/rpms/package_version.sh argobots debug)")
pkgs+=("$(utils/rpms/package_version.sh daos_spdk dev)")
pkgs+=("$(utils/rpms/package_version.sh daos_spdk debug)")
pkgs+=("$(utils/rpms/package_version.sh isal dev)")
pkgs+=("$(utils/rpms/package_version.sh isal_crypto lib)")
pkgs+=("$(utils/rpms/package_version.sh isal_crypto debug)")
pkgs+=("$(utils/rpms/package_version.sh libfabric dev)")
pkgs+=("$(utils/rpms/package_version.sh libfabric debug)")
pkgs+=("$(utils/rpms/package_version.sh mercury dev)")
pkgs+=("$(utils/rpms/package_version.sh mercury debug)")
pkgs+=("$(utils/rpms/package_version.sh pmdk lib pmemobj)")
pkgs+=("$(utils/rpms/package_version.sh pmdk debug pmemobj)")
pkgs+=("$(utils/rpms/package_version.sh pmdk debug pmem)")
pkgs+=("fuse3")
pkgs+=("gotestsum")
pkgs+=("hwloc-devel")
pkgs+=("libasan")
pkgs+=("libipmctl-devel")
pkgs+=("libyaml-devel")
pkgs+=("numactl")
pkgs+=("numactl-devel")
pkgs+=("openmpi${OPENMPI_VER}")
pkgs+=("patchelf")
pkgs+=("pciutils-devel")
pkgs+=("protobuf-c")
pkgs+=("valgrind-devel")

if [ "${code_coverage}" == "true" ] ; then
    pkgs+=("$(utils/rpms/package_version.sh bullseye normal)")
fi

# output with trailing newline suppressed
printf "${pkgs[*]}"
exit 0
