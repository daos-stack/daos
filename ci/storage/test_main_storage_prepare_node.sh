#!/bin/bash

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
        --disable daos-stack-daos-"${DISTRO_GENERIC}"-"${VERSION_ID%%.*}"-x86_64-stable-local-artifactory
fi
dnf -y install ipmctl daos-server"$DAOS_PKG_VERSION"

lspci | grep Mellanox
lscpu | grep Virtualization
lscpu | grep -E -e Socket -e NUMA

if command -v opainfo; then opainfo || true; fi

if command -v ibv_devinfo; then ibv_devinfo || true; fi

lspci | grep -i "Non-Volatile memory controller" || true

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
    if ip addr show ib1; then
        # All of our CI nodes with two ib adapters should have PMEM DIMMs
        echo 'No PMEM DIMM devices found on CI node!'
        exit 1
    else
        echo 'No PMEM DIMM devices found!'
    fi
fi
