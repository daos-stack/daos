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

env

pushd "${mydir}/../.." || exit 1
utils/rpms/build_packages.sh daos
mkdir -p /home/daos/rpms
cp ./*.rpm /home/daos/rpms
popd || exit 1
