#!/bin/bash
set -uex

bullseye_key="${1:-}"

# Get the bullseye version
mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
pushd "${mydir}/../.." || exit 1
source utils/rpms/package_info.sh
popd

: "${SL_BULLSEYE_PREFIX:=/opt/BullseyeCoverage}"
bullseye_url="https://www.bullseye.com/download-archive/${bullseye_version%.*}"
bullseye_src="${bullseye_url}/BullseyeCoverage-${bullseye_version}-Linux-x64.tar.xz"
bullseye_out="bullseye.tar.xz"

if [ -n "${DAOS_HTTPS_PROXY:-}" ]; then
    curl --proxy "${DAOS_HTTPS_PROXY}" "${bullseye_src}" --retry 10 --retry-max-time 60 --silent --show-error -o "${bullseye_out}"
else
    curl "${bullseye_src}" --retry 10 --retry-max-time 60 --silent --show-error -o "${bullseye_out}"
fi

mkdir -p bullseye
tar -C bullseye --strip-components=1 -xf "${bullseye_out}"
pushd bullseye
set +x
echo + sudo ./install --quiet --key "**********" --prefix "${SL_BULLSEYE_PREFIX}"
sudo ./install --quiet --key "${bullseye_key}" --prefix "${SL_BULLSEYE_PREFIX}"
set -x
popd
# rm -rf bullseye.tar.xz bullseye
# ls -alR "${SL_BULLSEYE_PREFIX}"
