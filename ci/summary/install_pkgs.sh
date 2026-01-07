#!/bin/bash
#
#  Copyright 2026 Hewlett Packard Enterprise Development LP
#
# Script for installing packages used for CI summary steps
set -uex

id
if [ "$(id -u)" = "0" ]; then
    echo "Should not be run as root"
    exit 1
fi

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
files=("$mydir/../parse_ci_envs.sh")
files+=("$mydir/../provision/post_provision_config_common_functions.sh")
for src_file in "${files[@]}"; do
    if [ -e "${src_file}" ]; then
        # shellcheck source=parse_ci_envs.sh disable=SC1091
        source "${src_file}"
    fi
done

env | sort -n

# Add a repo for this build
add_inst_repo "daos" "${BRANCH_NAME}" "${BUILD_NUMBER}"

# Install bullseye
bullseye_pkg="$(utils/rpms/package_version.sh bullseye normal)"
sudo dnf install --allowerasing -y "${bullseye_pkg}" || echo "${bullseye_pkg} not available"

# Install bullshtml
bullshtml_vers=1.0.5
bullshtml_src=https://storage.googleapis.com/google-code-archive-downloads/v2/code.google.com/bullshtml
bullshtml_tar="bullshtml_${bullshtml_vers}.tar.gz"
if [ -n "${DAOS_HTTPS_PROXY:-}" ]; then
    export https_proxy="${DAOS_HTTPS_PROXY}"
fi
sudo dnf install -y wget
wget "${bullshtml_src}/${bullshtml_tar}"
tar --strip-components=1 -xf "${bullshtml_tar}"
exit 0

# bullshtml_pkg="$(utils/rpms/package_version.sh bullshtml normal)"
# sudo dnf install --allowerasing -y "${bullshtml_pkg}" || echo "${bullshtml_pkg} not available"
