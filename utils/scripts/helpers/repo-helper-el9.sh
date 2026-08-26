#!/bin/bash
#
#  Copyright 2023 Intel Corporation.
#  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
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

# dnf5 dropped wildcard repo name support in the config-manager plugin.
# Where dnf5 is the default (fedora now, possibly el-11) "dnf4" is the
# supported workaround, so make sure a "dnf4" always exists and use it for
# repo name matching.
ensure_dnf4 () {
    if command -v dnf4 > /dev/null; then
        return
    fi
    mkdir -p /usr/local/bin
    ln -s "$(command -v dnf)" /usr/local/bin/dnf4
}

# Enable the daos deps repos.  The only naming rule that can be relied on
# across sites is that the repo id contains "daos" followed by "deps".
enable_deps_repos () {
    local repo
    local deps_repos
    mapfile -t deps_repos < <(dnf4 repolist --all -q 2>/dev/null |
                              awk '{print $1}' | grep -i 'daos.*deps' || true)
    for repo in "${deps_repos[@]:-}"; do
        if [ -z "$repo" ]; then
            continue
        fi
        dnf4 config-manager --enable "$repo"
    done
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
repos_dir=/etc/yum.repos.d/
if [ -n "$REPO_FILE_URL" ]; then
    install_curl
    install_optional_ca
    mkdir -p "$repos_dir"
    pushd "$repos_dir"
    curl -k --noproxy '*' -sSf -o "daos_ci-el${MAJOR_VER}-${REPOSITORY_NAME}.repo"  \
         "${REPO_FILE_URL}daos_ci-el${MAJOR_VER}-${REPOSITORY_NAME}.repo"
    disable_repos "$repos_dir"
    popd
fi
dnf -y --disablerepo \*epel\* install dnf-plugins-core
ensure_dnf4
dnf -y config-manager --save --setopt=assumeyes=True
dnf config-manager --save --setopt=install_weak_deps=False
dnf --disablerepo \*epel\* install epel-release
if [ -n "$REPO_FILE_URL" ]; then
    PT_REPO="daos_ci-${DISTRO}${MAJOR_VER}-crb-${REPOSITORY_NAME}"
    true > "${repos_dir}epel.repo"
    true > "${repos_dir}epel-modular.repo"
    sed "s/^mirrorlist_expire=0*/mirrorlist_expire=99999999/" \
        -i /etc/dnf/dnf.conf
else
    PT_REPO=crb
fi
dnf -y install epel-release
dnf config-manager --enable "$PT_REPO"
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
module_hotfixes=true\n" >> "$repos_dir$repo:$branch:$build_number".repo
    cat "$repos_dir$repo:$branch:$build_number".repo
    save_repos+=("$repo:$branch:$build_number")
done

disable_repos "$repos_dir" "${save_repos[@]}"
enable_deps_repos

if [ -n "$REPO_FILE_URL" ]; then
# Calculate trusted-host and trusted_base_url for artifactory/repository
    repo_url_scheme="${REPO_FILE_URL%%://*}"
    repo_url_no_scheme="${REPO_FILE_URL#*://}"
    trusted_host_port="${repo_url_no_scheme%%/*}"
    trusted_host="${trusted_host_port%%:*}"
    first_path_element="${repo_url_no_scheme#*/}"
    first_path_element="${first_path_element%%/*}"
    trusted_base_url="${repo_url_scheme}://${trusted_host_port}/${first_path_element}"

# Setup pip/uv to use the proxy only when the endpoint is reachable.
    pypi_proxy_url="${trusted_base_url}/api/pypi/pypi-proxy/simple"
    if curl -k --noproxy '*' -fsS --connect-timeout 5 --max-time 10 \
        "${pypi_proxy_url}" > /dev/null 2>&1; then
        cat <<EOF > /etc/pip.conf
[global]
    trusted-host = ${trusted_host}
    index-url = ${pypi_proxy_url}
    progress_bar = off
    no_color = true
    quiet = 1
EOF
    else
        echo "Skipping pip proxy setup: ${pypi_proxy_url} is unreachable"
    fi

# Setup RubyGems to use artifactory/repository as the installation source only
# when the endpoint is reachable.
    gem_proxy_url="${trusted_base_url}/api/gems/rubygems-proxy/"
    if curl -k --noproxy '*' -fsS --connect-timeout 5 --max-time 10 \
        "${gem_proxy_url}" > /dev/null 2>&1; then
        cat <<EOF > /etc/gemrc
:sources:
- ${gem_proxy_url}
EOF
    else
        echo "Skipping /etc/gemrc setup: ${gem_proxy_url} is unreachable"
    fi
fi
