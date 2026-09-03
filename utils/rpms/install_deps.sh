#!/bin/bash
#
#  (C) Copyright 2025 Google LLC
#  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Script for installing DAOS dependencies for the expected version
set -uex

id
if [ "$(id -u)" = "0" ]; then
    echo "Should not be run as root"
    exit 1
fi

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

env

export DISTRO="${1}"
libfabric_pkg="$(${mydir}/package_version.sh libfabric dev)"
mercury_pkg="$(${mydir}/package_version.sh mercury dev)"
argobots_pkg="$(${mydir}/package_version.sh argobots dev)"
fused_pkg="$(${mydir}/package_version.sh fused dev)"
isal_pkg="$(${mydir}/package_version.sh isal dev)"
isal_crypto_pkg="$(${mydir}/package_version.sh isal_crypto dev)"
daos_spdk_pkg="$(${mydir}/package_version.sh daos_spdk dev)"
pmdk_pkg="$(${mydir}/package_version.sh pmdk dev pmemobj)"

sudo dnf install --allowerasing -y "${libfabric_pkg}" || echo "${libfabric_pkg} not available"
sudo dnf install --allowerasing -y "${mercury_pkg}" || echo "${mercury_pkg} not available"
sudo dnf install --allowerasing -y "${argobots_pkg}" || echo "${argobots_pkg} not available"
sudo dnf install --allowerasing -y "${daos_spdk_pkg}" || echo "${daos_spdk_pkg} not available"
sudo dnf install --allowerasing -y "${fused_pkg}" || echo "${fused_pkg} not available"
sudo dnf install --allowerasing -y "${pmdk_pkg}" || echo "${pmdk_pkg} not available"
sudo dnf install --allowerasing -y "${isal_pkg}" || echo "${isal_pkg} not available"
sudo dnf install --allowerasing -y "${isal_crypto_pkg}" || echo "${isal_crypto_pkg} not available"
