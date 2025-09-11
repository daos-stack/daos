#!/bin/bash

# Script for installing DAOS dependencies for the expected version
set -uex

id
if [ "$(id -u)" = "0" ]; then
    echo "Should not be run as root"
    exit 1
fi

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ci_envs="$mydir/../parse_ci_envs.sh"
if [ -e "${ci_envs}" ]; then
  # shellcheck source=parse_ci_envs.sh disable=SC1091
  source "${ci_envs}"
fi

env

pushd "${mydir}/../.." || exit 1
export DISTRO="${1}"
export DAOS_RELVAL="${2}"
libfabric_pkg="$(utils/rpms/package_version.sh libfabric dev)"
mercury_pkg="$(utils/rpms/package_version.sh mercury dev)"
argobots_pkg="$(utils/rpms/package_version.sh argobots dev)"
fused_pkg="$(utils/rpms/package_version.sh fused dev)"
isal_pkg="$(utils/rpms/package_version.sh isal dev)"
isal_crypto_pkg="$(utils/rpms/package_version.sh isal_crypto dev)"
daos_spdk_pkg="daos-spdk-devel$(utils/rpms/package_version.sh daos_spdk dev)"
pmdk_pkg="$(utils/rpms/package_version.sh pmdk dev pmemobj)"

sudo dnf install --allowerasing -y "${libfabric_pkg}" || echo "${libfabric_pkg} not available"
sudo dnf install --allowerasing -y "${mercury_pkg}" || echo "${mercury_pkg} not available"
sudo dnf install --allowerasing -y "${argobots_pkg}" || echo "${argobots_pkg} not available"
sudo dnf install --allowerasing -y "${daos_spdk_pkg}" || echo "${daos_spdk_pkg} not available"
sudo dnf install --allowerasing -y "${fused_pkg}" || echo "${fused_pkg} not available"
sudo dnf install --allowerasing -y "${pmdk_pkg}" || echo "${pmdk_pkg} not available"
sudo dnf install --allowerasing -y "${isal_pkg}" || echo "${isal_pkg} not available"
sudo dnf install --allowerasing -y "${isal_crypto_pkg}" || echo "${isal_crypto_pkg} not available"
popd || exit 1
