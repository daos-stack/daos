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
    dnf -y install python39 python39-devel
    dnf -y install python3.11 python3.11-devel
}
