#!/bin/bash
#
#  Copyright 2021-2023 Intel Corporation.
#  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -eux

: "${STORAGE_PREP_OPT:=}"
: "${STORAGE_SCAN:=}"

if [ -n "$DAOS_PKG_VERSION" ]; then
    DAOS_PKG_VERSION="-$DAOS_PKG_VERSION"
else
    # don't need artifactory if no version was specified,
    # as that means we are using the packages in the build
    # shellcheck disable=SC1091
    . /etc/os-release
    case "$ID_LIKE" in
        *rhel*)
            DISTRO_GENERIC=el
            ;;
        *suse*)
            DISTRO_GENERIC=sl
            ;;
    esac
    dnf -y config-manager \
        --disable daos-stack-daos-"${DISTRO_GENERIC}"-"${VERSION_ID%%.*}"*-stable-local-artifactory
fi
# this needs to be made more generic in the future.
dnf -y config-manager \
        --enable daos-stack-deps-"${DISTRO_GENERIC}"-"${VERSION_ID%%.*}"*-stable-local-artifactory

dnf -y install ipmctl daos-server"$DAOS_PKG_VERSION"

lspci | grep Mellanox || true
lscpu | grep Virtualization || true
lscpu | grep -E -e Socket -e NUMA

if command -v opainfo; then opainfo || true; fi

if command -v ibv_devinfo; then ibv_devinfo || true; fi

lspci | grep -i "Non-Volatile memory controller" || true

ib_count=0
for ib_path in /sys/class/net/ib*; do
    if [ ! -e "$ib_path" ]; then
        continue
    fi
    ((ib_count++)) || true
    ip addr show "$(basename "$ib_path")"
done

# Skip test controller
if [ "$ib_count" -le 1 ]; then
    echo "Less than 2 Infiniband devices found ($ib_count)."
    echo "Assuming this is a test controller node. Skipping PMEM setup."
    exit
fi

if ipmctl show -dimm; then
    ipmctl show -goal
    ipmctl show -region
    find /dev -name 'pmem*'

    daos_server scm "$STORAGE_PREP_OPT"  --force

    if [ -n "$STORAGE_SCAN" ]; then
      # if we don't have pmem here, then we have a problem.
      daos_server scm scan
      if ! ls -l /dev/pmem*; then
          echo 'No /dev/pmem* devices found when checking storage!'
          exit 1
      fi
    fi
else
    if [ "$ib_count" -ge 2 ]; then
        # All of our CI nodes with two ib adapters should have PMEM DIMMs
        echo 'No PMEM DIMM devices found on CI node!'
        exit 1
    else
        echo 'No PMEM DIMM devices found!'
    fi
fi
