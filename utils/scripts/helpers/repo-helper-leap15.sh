#!/bin/bash
#
#  Copyright 2022-2023 Intel Corporation.
#  Copyright 2024-2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -uex

# This script is used by dockerfiles to optionally use
# a local repository instead of a distro provided repository.
# It will also optionally allow running a /tmp/install script
# for custom packages if present.

: "${REPO_FILE_URL:=}"
: "${BASE_DISTRO:=opensuse/leap:15.6}"
: "${JENKINS_URL:=}"
: "${REPOS:=}"
: "${DAOS_LAB_CA_FILE_URL:=}"
: "${REPOSITORY_NAME:=artifactory}"

# shellcheck disable=SC2120
disable_repos () {
    local repos_dir="$1"
    shift
    local save_repos
    IFS=" " read -r -a save_repos <<< "${*:-} daos_ci-leap15-${REPOSITORY_NAME}"
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

    if command -v curl; then
        echo "found curl!"
        return
    fi

    if command -v wget; then
        echo "found wget!"
        return
    fi
}

install_dnf() {

    if command -v dnf; then
        dnf -y install dnf-plugins-core
        return
    fi

    # DO NOT do a "zypper mr --all --disable" here as it rewrites the repo
    # files, removing options that zypper doesn't use but dnf does
    if ! zypper --non-interactive --gpg-auto-import-keys install dnf{,-plugins-core}; then
        rc=${PIPESTATUS[0]}
        echo "Got an $rc error installing dnf?" >&2
        exit "$rc"
    fi
    zypper removerepo temp_opensuse_oss_proxy
}

# Use local repo server if present
install_optional_ca() {
    ca_storage="/etc/pki/trust/anchors/"
    if [ -n "$DAOS_LAB_CA_FILE_URL" ]; then
        curl -k -sSf -o "${ca_storage}lab_ca_file.crt" \
            "$DAOS_LAB_CA_FILE_URL"
        update-ca-certificates
    fi
}

# Use local repo server if present
# if a local repo server is present and the distro repo server can not
# be reached, have to bootstrap in an environment to get curl installed
# to then install the pre-built repo file.

MAJOR_VER="${BASE_DISTRO##*:}"
MAJOR_VER="${MAJOR_VER%%.*}"
if command -v dnf; then
    repos_dir=/etc/yum.repos.d/
else
    repos_dir=/etc/zypp/repos.d/
fi
if [ -n "$REPO_FILE_URL" ]; then
    install_curl
    install_optional_ca
    mkdir -p "$repos_dir"
    pushd "$repos_dir"
    curl -sSf -o "daos_ci-leap${MAJOR_VER}-${REPOSITORY_NAME}.repo" \
         "${REPO_FILE_URL}daos_ci-leap${MAJOR_VER}-${REPOSITORY_NAME}.repo"
    disable_repos "$repos_dir"
    popd
    # # These may have been created in the Dockerfile must be removed
    # # when using a local repository.
    # unset HTTPS_PROXY
    # unset https_proxy
else
    # shellcheck disable=SC1091
    . /etc/os-release

    # network:cluster is only published for some Leap versions (e.g. not
    # 15.4). Probe the GPG key URL first so unsupported versions (such as
    # GitHub Actions using an older Leap) build without this repo instead
    # of failing outright.
    network_cluster_key_url="https://download.opensuse.org/repositories/network:/cluster/${VERSION_ID}/repodata/repomd.xml.key"
    if curl -fsS --connect-timeout 5 --max-time 10 \
        "${network_cluster_key_url}" > /dev/null 2>&1; then
        zypper --non-interactive addrepo --gpgcheck \
            "https://download.opensuse.org/repositories/network:/cluster/${VERSION_ID}/" \
            network-cluster
        rpm --import "${network_cluster_key_url}"
    else
        echo "Skipping network-cluster repo: ${network_cluster_key_url} is unavailable for ${VERSION_ID}"
    fi
fi
install_dnf
if [ ! -d /etc/yum.repos.d/ ]; then
    mkdir -p /etc/yum.repos.d/
    pushd "$repos_dir"
    for file in *.repo; do
        if [ ! -s "$file" ]; then
            continue
        fi
        echo "Fixing $file"
        sed -e '/type=NONE/d' < "$file" > "/etc/yum.repos.d/$file"
    done
    popd
fi
if command -v zypper; then
    zypper --non-interactive clean --all
fi
dnf config-manager --save --setopt=assumeyes=True
dnf config-manager --save --setopt=install_weak_deps=False

daos_base="job/daos-stack/job/"
artifacts="/artifact/artifacts/leap15/"
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

if [ -e /etc/profile.d/lmod.sh ]; then
    if ! grep "MODULEPATH=.*/usr/share/modules" /etc/profile.d/lmod.sh; then
        sed -e '/MODULEPATH=/s/$/:\/usr\/share\/modules/' \
               /etc/profile.d/lmod.sh
    fi
fi

# This command should not be needed, but docker containers for leap 15 have
# in the past failed to validate HTTPS certificates if this command is not
# run here.  Running this command just makes sure things work.
update-ca-certificates

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
    if curl -fsS --connect-timeout 5 --max-time 10 \
        "${pypi_proxy_url}" > /dev/null 2>&1; then
        {
            echo '[global]'
            echo "    trusted-host = ${trusted_host}"
            echo "    index-url = ${pypi_proxy_url}"
        } > /etc/pip.conf
    elif [ -n "${DAOS_HTTPS_PROXY:-}" ]; then
        {
            echo '[global]'
            echo "    proxy = ${DAOS_HTTPS_PROXY}"
        } > /etc/pip.conf
    else
        echo '[global]' > /etc/pip.conf
    fi
    # Options common to every /etc/pip.conf variant above.
    {
        echo '    progress_bar = off'
        echo '    no_color = true'
        echo '    quiet = 1'
    } >> /etc/pip.conf

# Setup RubyGems to use artifactory/repository as the installation source only
# when the endpoint is reachable.
    gem_proxy_url="${trusted_base_url}/api/gems/rubygems-proxy/"
    # Remove any stale rpmorig so a later ruby-devel install cannot leave it
    # behind under a different name, breaking the restore of this gemrc.
    rm -f /etc/gemrc.rpmorig
    # Options common to every /etc/gemrc variant below.
    {
        echo '---'
        echo 'gem: --no-document --quiet'
    } > /etc/gemrc
    if curl -fsS --connect-timeout 5 --max-time 10 \
        "${gem_proxy_url}" > /dev/null 2>&1; then
        {
            echo ':sources:'
            echo "- ${gem_proxy_url}"
        } >> /etc/gemrc
    elif [ -n "${DAOS_HTTPS_PROXY:-}" ]; then
        {
            echo "http_proxy: ${DAOS_HTTPS_PROXY}"
            echo "https_proxy: ${DAOS_HTTPS_PROXY}"
        } >> /etc/gemrc
    else
        echo "Skipping gem source/proxy setup: ${gem_proxy_url} is unreachable"
    fi
fi
