#!/bin/bash
set -uex

bullseye_key="${1:-}"

# Get the bullseye version
mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
pushd "${mydir}/../.." || exit 1
source utils/rpms/package_info.sh
popd

bullseye_url="https://www.bullseye.com/download"
bullseye_src="${bullseye_url}/BullseyeCoverage-${bullseye_version}-Linux-x64.tar.xz"
bullseye_out="bullseye.tar.xz"
# export SL_BULLSEYE_PREFIX="/opt/BullseyeCoverage"

proxy=""
if [ -n "${DAOS_HTTPS_PROXY:-}" ]; then
    proxy="--proxy ${DAOS_HTTPS_PROXY}"
fi

curl "${proxy}" "${bullseye_src}" --retry 10 --retry-max-time 60 --silent --show-error -o "${bullseye_out}"
mkdir -p bullseye
tar -C bullseye --strip-components=1 -xf "${bullseye_out}"
pushd bullseye
set +x
echo + sudo ./install --quiet --key "**********" --prefix "${SL_BULLSEYE_PREFIX}"
sudo ./install --quiet --key "${bullseye_key}" --prefix "${SL_BULLSEYE_PREFIX}"
set -x
popd
rm -rf bullseye.tar.xz bullseye
# ls -alR "${SL_BULLSEYE_PREFIX}"
