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

# Distro name for the repository path for accessing packages built by CI
export DISTRO_NAME="${1:-el9}"

# Import provisioning functions to add the repo
mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
files=("$mydir/../parse_ci_envs.sh")
files+=("$mydir/../provisioning/post_provision_config_common_functions.sh")
for src_file in "${files[@]}"; do
    if [ -e "${src_file}" ]; then
        # shellcheck source=parse_ci_envs.sh disable=SC1091
        source "${src_file}"
    fi
done

env | sort -n

# Run the remainder of the script as root to be able to install packages
exec sudo -- "$0" "$@"

# Add the repo for packages built by CI
add_inst_repo "daos" "${BRANCH_NAME}" "${BUILD_NUMBER}" "true"

# Install bullseye
bullseye_pkg="$(utils/rpms/package_version.sh bullseye normal)"
dnf install --allowerasing -y "${bullseye_pkg}" || echo "${bullseye_pkg} not available"
