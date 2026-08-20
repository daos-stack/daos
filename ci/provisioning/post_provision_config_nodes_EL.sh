#!/bin/bash
#
#  Copyright 2021-2024 Intel Corporation.
#  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
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

    # Use a more recent python version for unit testing, this allows us to also test installing
    # pydaos into virtual environments.
    : "${PYTHON_VERSION:=3.11}"
    dnf -y install "python${PYTHON_VERSION}" "python${PYTHON_VERSION}-devel"

    # Configure IPoIB for any InfiniBand interfaces present.
    # The base OS image may not include ifcfg files for IB devices; create them
    # with DHCP if absent so the hardware check can verify IB connectivity.
    local _ib_configured=false
    for _iface_path in /sys/class/net/ib*; do
        [[ -e "$_iface_path" ]] || continue
        _dev=$(basename "$_iface_path")
        _ifcfg="/etc/sysconfig/network-scripts/ifcfg-${_dev}"
        if [[ ! -f "$_ifcfg" ]]; then
            cat > "$_ifcfg" << EOF
DEVICE=${_dev}
ONBOOT=yes
TYPE=InfiniBand
BOOTPROTO=dhcp
DEFROUTE=no
DHCLIENT_SET_DEFAULT_ROUTE=no
CONNECTED_MODE=no
EOF
            _ib_configured=true
        fi
    done
    if $_ib_configured; then
        nmcli con reload
        for _iface_path in /sys/class/net/ib*; do
            [[ -e "$_iface_path" ]] || continue
            _dev=$(basename "$_iface_path")
            nmcli device connect "$_dev" || true
        done
    fi
}
