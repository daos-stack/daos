#!/bin/bash
code_coverage="${1:-false}"
bullseye_key="${2:-}"

cd /home/daos/pre || exit 1
scons install --build-deps=only USE_INSTALLED=all PREFIX=/opt/daos TARGET_TYPE=release -j 32

if [ "${code_coverage}" == "true" ] ; then
    https_proxy="http://proxy.houston.hpecorp.net:8080/"
    curl https://www.bullseye.com/download/BullseyeCoverage-9.23.7-Linux-x64.tar.xz --retry 10 --retry-max-time 60 --silent --show-error -o bullseye.tar.xz
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
fi
