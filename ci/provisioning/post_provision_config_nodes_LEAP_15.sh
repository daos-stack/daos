#!/bin/bash

bootstrap_dnf() {
    rm -rf "$REPOS_DIR"
    ln -s ../zypp/repos.d "$REPOS_DIR"
    dnf config-manager --save --setopt allow_vendor_change=True
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

    # force install of avocado 69.x
    dnf -y erase avocado{,-common}                                              \
                 python2-avocado{,-plugins-{output-html,varianter-yaml-to-mux}}
    python3 -m pip install --upgrade pip
    python3 -m pip install "avocado-framework<70.0"
    python3 -m pip install "avocado-framework-plugin-result-html<70.0"
    python3 -m pip install "avocado-framework-plugin-varianter-yaml-to-mux<70.0"
}
