#!/bin/bash
#
#  (C) Copyright 2021-2023 Intel Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent

bootstrap_dnf() {
    systemctl enable postfix.service
    systemctl start postfix.service
}

group_repo_post() {
    # Nothing to do for EL
    :
}

distro_custom() {
    # install avocado
    local avocado_rpms=(python3-avocado{,-plugins-{output-html,varianter-yaml-to-mux}})
    if [ -z "$(dnf repoquery "${avocado_rpms[@]}")" ]; then
        avocado_rpms=()
        pip install "avocado-framework<83.0"
        pip install "avocado-framework-plugin-result-html<83.0"
        pip install "avocado-framework-plugin-varianter-yaml-to-mux<83.0"
    fi
    dnf -y install "${avocado_rpms[@]}" clustershell

    # for Launchable's pip install
    dnf -y install python3-setuptools.noarch

}

install_mofed() {
    if [ -z "$MLNX_VER_NUM" ]; then
        echo "MLNX_VER_NUM is not set"
        env
        exit 1
    fi

    # Remove omnipath software
    # shellcheck disable=SC2046
    time dnf -y remove $(rpm -q opa-address-resolution \
                                opa-basic-tools \
                                opa-fastfabric \
                                opa-libopamgt \
                                compat-openmpi16 \
                                compat-openmpi16-devel \
                                openmpi \
                                openmpi-devel \
                                ompi \
                                ompi-debuginfo \
                                ompi-devel | grep -v 'is not installed')


    stream=false
    gversion="$VERSION_ID"
    if [ "$gversion" == "8" ]; then
        # Mellanox does not have a release for 8.9 yet.
        gversion="8.8"
        stream=true
     elif [[ $gversion = *.*.* ]]; then
        gversion="${gversion%.*}"
    fi

    # Add a repo to install MOFED RPMS
    artifactory_base_url="https://artifactory.dc.hpdd.intel.com/artifactory/"
    mellanox_proxy="${artifactory_base_url}mellanox-proxy/mlnx_ofed/"
    mellanox_key_url="${artifactory_base_url}mlnx_ofed/RPM-GPG-KEY-Mellanox"
    rpm --import "$mellanox_key_url"
    repo_url="$mellanox_proxy$MLNX_VER_NUM/rhel$gversion/x86_64/"
    dnf -y config-manager --add-repo="$repo_url"
    dnf -y config-manager --save --setopt="$(url_to_repo "$repo_url")".gpgcheck=1
    dnf repolist || true

    time dnf -y install mlnx-ofed-basic ucx-cma ucx-ib ucx-knem ucx-rdmacm ucx-xpmem

    # now, upgrade firmware
    time dnf -y install mlnx-fw-updater

    # Make sure that tools are present.
    #ls /usr/bin/ib_* /usr/bin/ibv_*

    dnf list --showduplicates perftest
    if [ "$gversion" == "8.5" ]; then
        dnf remove -y perftest || true
    fi
    if $stream; then
        dnf list --showduplicates ucx-knem
        dnf remove -y ucx-knem || true
    fi

}
