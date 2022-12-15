#!/bin/bash
set -uex

# This script is used by dockerfiles to optionally use
# a local repository instead of a distro provided repository.
# It will also optionally allow running a /tmp/install script
# for custom packages if present.

: "${REPO_FILE_URL:=}"
: "${BASE_DISTRO:=rockylinux/rockylinux:8}"
: "${JENKINS_URL:=}"
: "${REPOS:=}"

# shellcheck disable=SC2120
disable_repos () {
    local repos_dir="$1"
    shift
    local save_repos
    IFS=" " read -r -a save_repos <<< "${*:-} daos_ci-el8-artifactory"
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

# installs/upgrades of epel-release add repos
# Disable mirrorlist check when using local repos.
DISTRO="rocky"
if [[ $BASE_DISTRO == *alma* ]]; then
    DISTRO='alma'
fi
MAJOR_VER="${BASE_DISTRO##*:}"
MAJOR_VER="${MAJOR_VER%%.*}"
if [ -n "$REPO_FILE_URL" ]; then
    install_curl
    mkdir -p /etc/yum.repos.d
    pushd /etc/yum.repos.d/
    curl -k -f -o daos_ci-el$MAJOR_VER-artifactory.repo        \
         "$REPO_FILE_URL"daos_ci-el$MAJOR_VER-artifactory.repo
    disable_repos /etc/yum.repos.d/
    popd
fi
dnf --assumeyes --disablerepo \*epel\* install dnf-plugins-core
dnf config-manager --save --setopt=assumeyes=True
dnf config-manager --save --setopt=install_weak_deps=False
if [ ! -f /etc/fedora-release ]; then
    dnf --disablerepo \*epel\* install epel-release
    if [ -n "$REPO_FILE_URL" ]; then
        PT_REPO="daos_ci-${DISTRO}${MAJOR_VER}-powertools-artifactory"
        true > /etc/yum.repos.d/epel.repo
        true > /etc/yum.repos.d/epel-modular.repo
        sed "s/^mirrorlist_expire=0*/mirrorlist_expire=99999999/" \
            -i /etc/dnf/dnf.conf
    else
        PT_REPO=powertools
    fi
    dnf install epel-release
        dnf config-manager --enable $PT_REPO
    fi
    dnf clean all

daos_base="job/daos-stack/job/"
artifacts="/artifact/artifacts/el8/"
save_repos=()
for repo in $REPOS; do
    branch="master"
    build_number="lastSuccessfulBuild"
    if [[ $repo = *@* ]]; then
        branch="${repo#*@}"
        repo="${repo%@*}"
        if [[ $branch = *:* ]]; then
            build_number="${branch#*:}"
            branch="${branch%:*}"
        fi
    fi
    echo -e "[$repo:$branch:$build_number]\n\
name=$repo:$branch:$build_number\n\
baseurl=${JENKINS_URL}$daos_base$repo/job/$branch/$build_number$artifacts\n\
enabled=1\n\
gpgcheck=False\n" >> /etc/yum.repos.d/$repo:$branch:$build_number.repo
    cat /etc/yum.repos.d/$repo:$branch:$build_number.repo
    save_repos+=("$repo:$branch:$build_number")
done

# Install OS updates and package.  Include basic tools and daos dependencies
if [ -e /tmp/install.sh ]; then
    dnf upgrade
    disable_repos /etc/yum.repos.d/ "${save_repos[@]}"
    /tmp/install.sh
    dnf clean all
    rm -f /tmp/install.sh
fi
