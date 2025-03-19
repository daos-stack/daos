#!/bin/bash
set -uex

# This script is used by dockerfiles to optionally use
# a local repository instead of a distro provided repository.
# It will also optionally allow running a /tmp/install script
# for custom packages if present.

: "${REPO_FILE_URL:=}"
: "${DAOS_LAB_CA_FILE_URL:=}"
: "${FVERSION:=latest}"
: "${REPOSITORY_NAME:=artifactory}"
: "${archive:=}"
if [ "$FVERSION" != "latest" ]; then
    archive="-archive"
fi

# shellcheck disable=SC2120
disable_repos () {
    local repos_dir="$1"
    shift
    local save_repos
    IFS=" " read -r -a save_repos <<< "${*:-} daos_ci-fedora${archive}-${REPOSITORY_NAME}"
    if [ -n "$REPO_FILE_URL" ]; then
        pushd "$repos_dir"
        local repo
        for repo in "${save_repos[@]}"; do
            mv "$repo".repo{,.tmp}
        done
        for file in *.repo; do
            true > "$file"
        done
        for repo in "${save_repos[@]}"; do
            mv "$repo".repo{.tmp,}
        done
        popd
    fi
}

# Use local repo server if present
install_curl() {
    :
}

# Use local repo server if present
install_optional_ca() {
    ca_storage="/etc/pki/ca-trust/source/anchors/"
    if [ -n "$DAOS_LAB_CA_FILE_URL" ]; then
        curl -k --noproxy '*' -sSf -o "${ca_storage}lab_ca_file.crt" \
            "$DAOS_LAB_CA_FILE_URL"
        update-ca-trust
    fi
}

# Use local repo server if present
# if a local repo server is present and the distro repo server can not
# be reached, have to bootstrap in an environment to get curl installed
# to then install the pre-built repo file.

if [ -n "$REPO_FILE_URL" ]; then
    install_curl
    install_optional_ca
    mkdir -p /etc/yum.repos.d
    pushd /etc/yum.repos.d/
    curl -k --noproxy '*' -sSf                                  \
         -o "daos_ci-fedora${archive}-${REPOSITORY_NAME}.repo"  \
         "{$REPO_FILE_URL}daos_ci-fedora${archive}-${REPOSITORY_NAME}.repo"
    disable_repos /etc/yum.repos.d/
    popd
fi
dnf -y install dnf-plugins-core
# This does not work in fedora/41 anymore -- needs investigation
# dnf -y config-manager --save --setopt=assumeyes=True
# dnf config-manager --save --setopt=install_weak_deps=False
dnf clean all

disable_repos /etc/yum.repos.d/ "${save_repos[@]}"
