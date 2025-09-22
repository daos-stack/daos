#!/bin/bash
#
#  Copyright 2021-2024 Intel Corporation.
#  Copyright 2025 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent

bootstrap_dnf() {
set +e
    systemctl enable postfix.service
    systemctl start postfix.service
    postfix_start_exit=$?
    if [ $postfix_start_exit -ne 0 ]; then
        echo "WARNING: Postfix not started: $postfix_start_exit"
        systemctl status postfix.service
        journalctl -xe -u postfix.service
    fi
set -e
    # Seems to be needed to fix some issues.
    dnf -y reinstall sssd-common
}

group_repo_post() {
    # Nothing to do for EL
    :
}

distro_custom() {
    # TODO: This code is not exiting on failure.
    : "${PYTHON_VERSION:=3.11}"

    # Use a more recent python version for unit testing, this allows us to also test installing
    # pydaos into virtual environments.
    dnf -y install python${PYTHON_VERSION} python${PYTHON_VERSION}-devel
}

install_mofed() {
    if [ -z "$MLNX_VER_NUM" ]; then
        echo "MLNX_VER_NUM is not set"
        env
        exit 1
    fi

    # Remove Omni-Path software
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

    : "${ARTIFACTORY_URL:=}"
    if [ -z "$ARTIFACTORY_URL" ]; then
        return
    fi

    # Install Mellanox OFED or DOCA RPMS
    install_mellanox="install_mellanox.sh"
    script_url="${ARTIFACTORY_URL}/raw-internal/sre_tools/$install_mellanox"
    install_target="/usr/local/sbin/$install_mellanox"

    if [ ! -e "$install_target" ]; then
        if ! curl --silent --show-error --fail \
            -o "/usr/local/sbin/$install_mellanox" "$script_url"; then
            echo "Failed to fetch $script_url"
            return 1
        fi
        chmod 0755 "$install_target"
    fi

    MELLANOX_VERSION="$MLNX_VER_NUM" "$install_mellanox"

    dnf list --showduplicates perftest
    if [ "$gversion" == "8.5" ]; then
        dnf remove -y perftest || true
    fi
    if $stream; then
        dnf list --showduplicates ucx-knem
        dnf remove -y ucx-knem || true
    fi
}
