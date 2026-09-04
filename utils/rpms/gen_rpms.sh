#!/bin/bash

# Script for building DAOS RPMs from a DAOS build
set -uex

id
if [ "$(id -u)" = "0" ]; then
    echo "Should not be run as root"
    exit 1
fi

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

env

pushd "${mydir}/../.." || exit 1
export DISTRO="${1}"
export DAOS_RELVAL="${2}"
rm -f ./*.rpm
rm -rf ./rpms/*
utils/rpms/build_packages.sh deps
if ls -1 ./*.rpm; then
  mkdir -p ./rpms/deps
  cp ./*.rpm ./rpms/deps
  rm -f ./*.rpm
fi
utils/rpms/build_packages.sh daos
mkdir -p ./rpms/daos
cp ./*.rpm ./rpms/daos
popd || exit 1
