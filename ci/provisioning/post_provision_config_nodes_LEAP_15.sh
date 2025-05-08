#!/bin/bash

bootstrap_dnf() {
    rm -rf "$REPOS_DIR"
    ln -s ../zypp/repos.d "$REPOS_DIR"
    dnf -y remove lua-lmod
    dnf -y install lua-lmod '--repo=*lua*' --repo '*network-cluster*'
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
    dnf remove -y lua-lmod
    dnf install -y --downloadonly lua-lmod --repo='*lua*' --repo '*network-cluster*'
}
