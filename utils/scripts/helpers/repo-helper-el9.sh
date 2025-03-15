#!/bin/bash
set -uex

# This script is used by dockerfiles to optionally use
# a local repository instead of a distro provided repository.
# It will also optionally allow running a /tmp/install script
# for custom packages if present.

: "${REPO_FILE_URL:=}"
: "${BASE_DISTRO:=rockylinux/rockylinux:$MAJOR_VER}"
: "${JENKINS_URL:=}"
: "${REPOS:=}"
: "${DAOS_LAB_CA_FILE_URL:=}"
: "${REPOSITORY_NAME:=artifactory}"

# shellcheck disable=SC2120
disable_repos () {
    local repos_dir="$1"
    shift
    local save_repos
    IFS=" " read -r -a save_repos <<< "${*:-} daos_ci-el$MAJOR_VER-${REPOSITORY_NAME}"
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

# installs/upgrades of epel-release add repos
# Disable mirrorlist check when using local repos.
DISTRO="rocky"
if [[ $BASE_DISTRO == *alma* ]]; then
    DISTRO='alma'
fi
# Use local repo server if present
# if a local repo server is present and the distro repo server can not
# be reached, have to bootstrap in an environment to get curl installed
# to then install the pre-built repo file.

MAJOR_VER="${BASE_DISTRO##*:}"
MAJOR_VER="${MAJOR_VER%%.*}"
if [ -n "$REPO_FILE_URL" ]; then
    install_curl
    install_optional_ca
    mkdir -p /etc/yum.repos.d
    pushd /etc/yum.repos.d/
    curl -k --noproxy '*' -sSf -o "daos_ci-el${MAJOR_VER}-${REPOSITORY_NAME}.repo"  \
         "${REPO_FILE_URL}daos_ci-el${MAJOR_VER}-${REPOSITORY_NAME}.repo"
    disable_repos /etc/yum.repos.d/
    popd
fi
dnf -y --disablerepo \*epel\* install dnf-plugins-core
dnf -y config-manager --save --setopt=assumeyes=True
dnf config-manager --save --setopt=install_weak_deps=False
if [ ! -f /etc/fedora-release ]; then
    dnf --disablerepo \*epel\* install epel-release
    if [ -n "$REPO_FILE_URL" ]; then
        PT_REPO="daos_ci-${DISTRO}${MAJOR_VER}-crb-${REPOSITORY_NAME}"
        true > /etc/yum.repos.d/epel.repo
        true > /etc/yum.repos.d/epel-modular.repo
        sed "s/^mirrorlist_expire=0*/mirrorlist_expire=99999999/" \
            -i /etc/dnf/dnf.conf
    else
        PT_REPO=crb
    fi
    dnf install epel-release
    dnf config-manager --enable "$PT_REPO"
fi
dnf clean all

daos_base="job/daos-stack/job/"
artifacts="/artifact/artifacts/el$MAJOR_VER/"
save_repos=()
for repo in $REPOS; do
    # don't install daos@ repos since we are building daos
    if [[ $repo = daos@* ]]; then
        continue
    fi
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
gpgcheck=False\n
module_hotfixes=true\n" >> /etc/yum.repos.d/"$repo:$branch:$build_number".repo
    cat /etc/yum.repos.d/"$repo:$branch:$build_number".repo
    save_repos+=("$repo:$branch:$build_number")
done

disable_repos /etc/yum.repos.d/ "${save_repos[@]}"
