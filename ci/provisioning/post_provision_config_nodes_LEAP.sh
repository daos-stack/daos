#!/bin/bash
#
#  Copyright 2021-2024 Intel Corporation.
#  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent

bootstrap_dnf() {
    rm -rf "$REPOS_DIR"
    ln -s ../zypp/repos.d "$REPOS_DIR"
    dnf -y remove lua-lmod
    dnf -y --nogpgcheck install lua-lmod --repo '*lua*' --repo '*network-cluster*' --repo '*oss-proxy*'
}

group_repo_post() {
    # Nothing to do for SL
    :
}

distro_custom() {
    # monkey-patch lua-lmod
    if ! grep MODULEPATH=".*"/usr/share/modules /etc/profile.d/lmod.sh; then \
        sed -e '/MODULEPATH=/s/$/:\/usr\/share\/modules/'                     \
               /etc/profile.d/lmod.sh;                                        \
    fi

    # Fix for no_pmix_multi_ctx tests on SLES/Leap 15.x
    if [[ "${VERSION_ID:-}" == 15.* ]]; then
        zypper rm -y -u mercury mercury-debuginfo || true
        zypper rm -y -u libfabric libfabric1 libfabric-debuginfo || true
        zypper clean --all
        ldconfig
        
        if [[ "${ID:-}" == "sles" ]]; then
            dnf install -y daos-server daos-client daos-client-tests \
                            daos-tests-internal openmpi3 openmpi3-devel
        else
            dnf install -y libfabric1 mercury-libfabric mercury
        fi
    fi
}
