#!/bin/bash
#
#  Copyright 2021-2024 Intel Corporation.
#  Copyright 2025 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent

bootstrap_dnf() {
    rm -rf "$REPOS_DIR"
    ln -s ../zypp/repos.d "$REPOS_DIR"
    dnf -y remove lua54 lua-lmod
    dnf -y --nogpgcheck install lua-lmod '--repo=*lua*' --repo '*network-cluster*'
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

    # Use a more recent python version for unit testing, this allows us to also test installing
    # pydaos into virtual environments.
    : "${PYTHON_VERSION:=}"
    dnf -y install "python${PYTHON_VERSION//./}" "python${PYTHON_VERSION//./}-devel"
    sudo update-alternatives --set python3 "/usr/bin/python${PYTHON_VERSION}"
    update-alternatives --list python3
}
