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
daos_spdk_pkg="$(utils/rpms/package_version.sh daos_spdk dev)"
pmdk_pkg="$(utils/rpms/package_version.sh pmdk dev pmemobj)"

dnf_install_args=(sudo dnf install --allowerasing -y)
if sudo dnf repolist --all | grep -q 'daos-stack-deps'; then
  dnf_install_args+=(--enablerepo='daos-stack-deps*')
fi

install_dep() {
  local package="$1"
  "${dnf_install_args[@]}" "${package}" || echo "${package} not available"
}

install_dep "${libfabric_pkg}"
install_dep "${mercury_pkg}"
install_dep "${argobots_pkg}"
install_dep "${daos_spdk_pkg}"
install_dep "${fused_pkg}"
install_dep "${pmdk_pkg}"
install_dep "${isal_pkg}"
install_dep "${isal_crypto_pkg}"
popd || exit 1
