#!/bin/bash

set -eux

yum install -y daos-server-"${DAOS_PKG_VERSION}"

lspci | grep Mellanox
lscpu | grep Virtualization
lscpu | grep -E -e Socket -e NUMA

if command -v opainfo; then opainfo || true; fi

if command -v ibv_devinfo; then ibv_devinfo || true; fi

if lspci | grep NVMe; then
  find /dev -name 'pmem*'

  daos_server storage prepare --scm-only --force

  find /dev -name 'pmem*'
else
  echo 'No NVMe devices found!'
fi
