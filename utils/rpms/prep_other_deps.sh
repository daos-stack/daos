#!/bin/bash
bullseye_key="${1:-}"

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
pushd "${mydir}/../.." || exit 1
source utils/rpms/package_info.sh
popd

curl --proxy=http://proxy.houston.hpecorp.net:8080/ \
    https://www.bullseye.com/download/BullseyeCoverage-${bullseye_version}-Linux-x64.tar.xz \
    --retry 10 --retry-max-time 60 --silent --show-error -o bullseye.tar.xz
mkdir -p bullseye
tar -C bullseye --strip-components=1 -xf bullseye.tar.xz
pushd bullseye
set +x
echo + sudo ./install --quiet --key "**********" --prefix /opt/BullseyeCoverage
sudo ./install --quiet --key "${bullseye_key}" --prefix /opt/BullseyeCoverage
set -x
ls -aR
ls -aR /opt/BullseyeCoverage
popd
rm -rf bullseye.tar.xz bullseye
