#!/bin/bash

# Script for installing packages used for CI summary steps
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

env | sort -n

# Install bullseye
bullseye_pkg="$(utils/rpms/package_version.sh bullseye normal)"
sudo dnf install --allowerasing -y "${bullseye_pkg}" || echo "${bullseye_pkg} not available"

# Install bullshtml
bullshtml_vers=1.0.5
bullshtml_src=https://code.google.com/archive/p/bullshtml/downloads
bullshtml_tar="bullshtml_${bullshtml_vers}.tar.gz"
if [ -n "${DAOS_HTTPS_PROXY:-}" ]; then
    curl --proxy "${DAOS_HTTPS_PROXY}" "${bullshtml_src}/${bullshtml_tar}" --retry 10 --retry-max-time 60 --silent --show-error -o "${bullshtml_tar}"
else
    curl "${bullshtml_src}/${bullshtml_tar}" --retry 10 --retry-max-time 60 --silent --show-error -o "${bullshtml_tar}"
fi
tar -C bullshtml --strip-components=1 -xf "${bullshtml_tar}"
pushd bullshtml
if [ ! -e build ]; then
    ./gradlew build
fi
popd
rm -f ./bullshtml.jar
cp bullshtml/build/libs/bullshtml-*.jar ./bullshtml.jar
# bullshtml_pkg="$(utils/rpms/package_version.sh bullshtml normal)"
# sudo dnf install --allowerasing -y "${bullshtml_pkg}" || echo "${bullshtml_pkg} not available"
