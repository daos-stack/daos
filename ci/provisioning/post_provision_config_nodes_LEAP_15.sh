#!/bin/bash

PYTHON3_VERSION="3.9"

bootstrap_dnf() {
    rm -rf "$REPOS_DIR"
    ln -s ../zypp/repos.d "$REPOS_DIR"
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
    dnf -y install python${PYTHON3_VERSION} python${PYTHON3_VERSION}-devel
    sudo update-alternatives --set python3 /usr/bin/python${PYTHON3_VERSION}
    update-alternatives --list python3
}
