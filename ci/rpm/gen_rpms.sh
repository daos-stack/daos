#!/bin/bash

# Script for building DAOS RPMs from a DAOS build
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

function mv_rpms() {
  local dir="${1}"
  if ls -1 ./*.rpm; then
    mkdir -p /home/daos/rpms/${dir}
    cp ./*.rpm /home/daos/rpms/${dir}
    rm -f ./*.rpm
  fi
}

env | sort -n

pushd "${mydir}/../.." || exit 1
export DISTRO="${1}"
export DAOS_RELVAL="${2}"
code_coverage="${3:-false}"
build_types="deps daos"
if [[ "${code_coverage}" == "true" ]]; then
  build_types="deps bullseye"
fi
rm -f ./*.rpm
rm -rf /home/daos/rpms/*
for build_type in ${build_types}; do
  utils/rpms/build_packages.sh "${build_type}"
  mv_rpms "${build_type}"
done
popd || exit 1
