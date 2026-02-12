#!/bin/bash
#
#  Copyright 2026 Hewlett Packard Enterprise Development LP
#
# Script for installing packages used for CI summary steps
set -uex

url_to_repo() {
    local url="$1"

    local repo=${url#*://}
    repo="${repo#/}"
    repo="${repo//%/}"
    repo="${repo//\//_}"

    echo "$repo"
}

add_inst_repo() {
    local repo="$1"
    local branch="$2"
    local build_number="$3"
    local distro="$4"
    local repo_url="${ARTIFACTS_URL:-${JENKINS_URL}job/}"daos-stack/job/"$repo"/job/"${branch//\//%252F}"/"$build_number"/artifact/artifacts/$distro/
    sudo dnf -y config-manager --add-repo="$repo_url"
    repo="$(url_to_repo "$repo_url")"
    # PR-repos: should always be able to upgrade modular packages
    sudo dnf -y config-manager --save --setopt "$repo.module_hotfixes=true" "$repo"
    disable_gpg_check "$repo_url"
}

disable_gpg_check() {
    local url="$1"

    repo="$(url_to_repo "$url")"
    # bug in EL7 DNF: this needs to be enabled before it can be disabled
    sudo dnf -y config-manager --save --setopt="$repo".gpgcheck=1
    sudo dnf -y config-manager --save --setopt="$repo".gpgcheck=0
    # but even that seems to be not enough, so just brute-force it
    if [ -d /etc/yum.repos.d ] &&
       ! grep gpgcheck /etc/yum.repos.d/"$repo".repo; then
        echo "gpgcheck=0" >> /etc/yum.repos.d/"$repo".repo
    fi
}

id
if [ "$(id -u)" = "0" ]; then
    echo "Should not be run as root"
    exit 1
fi

distro="${1:-el8}"

# mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# files=("$mydir/../parse_ci_envs.sh")
# files+=("$mydir/../provision/post_provision_config_common_functions.sh")
# for src_file in "${files[@]}"; do
#     if [ -e "${src_file}" ]; then
#         # shellcheck source=parse_ci_envs.sh disable=SC1091
#         source "${src_file}"
#     fi
# done

env | sort -n

# Add a repo for this build
add_inst_repo "daos" "${BRANCH_NAME}" "${BUILD_NUMBER}" "${distro}"

# Install bullseye
bullseye_pkg="$(utils/rpms/package_version.sh bullseye normal)"
sudo dnf install --allowerasing -y "${bullseye_pkg}" || echo "${bullseye_pkg} not available"

# # Install bullshtml
# bullshtml_vers=1.0.5
# bullshtml_src=https://storage.googleapis.com/google-code-archive-downloads/v2/code.google.com/bullshtml
# bullshtml_tar="bullshtml_${bullshtml_vers}.tar.gz"
# if [ -n "${DAOS_HTTPS_PROXY:-}" ]; then
#     export https_proxy="${DAOS_HTTPS_PROXY}"
# fi
# sudo dnf install -y wget
# wget "${bullshtml_src}/${bullshtml_tar}"
# tar --strip-components=1 -xf "${bullshtml_tar}"

# bullshtml_pkg="$(utils/rpms/package_version.sh bullshtml normal)"
# sudo dnf install --allowerasing -y "${bullshtml_pkg}" || echo "${bullshtml_pkg} not available"
