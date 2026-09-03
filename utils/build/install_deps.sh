#!/bin/bash
#
#  (C) Copyright 2025 Google LLC
#  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Script for installing DAOS dependencies for the expected version
#
# Usage:
#   install_deps.sh [DISTRO]
#
# Args:
#   DISTRO  Package distro suffix used by DAOS RPMs, e.g. el9, suse.lp155,
#           suse.lp156. This is not the OS distro itself, but the
#           standardized RPM naming suffix used by the DAOS project's own
#           package repos. If omitted, it is auto-detected from
#           /etc/os-release.
#
# Env:
#   DAOS_DEPS_EXT_REPO Optional dnf-format repo URL with a complete set of
#                      dependency RPMs, registered as an extra install source
#                      for the duration of this script only. Requires
#                      passwordless sudo to write/remove
#                      /etc/yum.repos.d/daos-deps-extra.repo.
set -uex

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=utils/build/build_utils.sh
source "${script_dir}/build_utils.sh"
rpms_dir="$(cd "${script_dir}/../rpms" >/dev/null 2>&1 && pwd)"

usage() {
    cat <<EOF
Usage: ${0##*/} [DISTRO]

Install pre-built DAOS dependency RPMs matching the versions expected by the
current tree.

Args:
    DISTRO  Package distro suffix used by DAOS RPMs (e.g. el9, suse.lp155,
            suse.lp156). If omitted, it is auto-detected from
            /etc/os-release.

Options:
    -h, --help  show this help and exit

Env:
    DAOS_DEPS_EXT_REPO  Optional dnf-format repo URL with a complete set of
                        dependency RPMs, registered as an extra install
                        source for the duration of this script only.
                        Requires passwordless sudo to write/remove
                        /etc/yum.repos.d/daos-deps-extra.repo.
EOF
}

check_help "$@"

# The script can be used only on el and leap/sles
validate_distro() {
    case "${1}" in
        el* | suse.lp15*)
            ;;
        *)
            echo "ERROR: unsupported DISTRO '${1}' (expected e.g. el9, suse.lp155, suse.lp156)"
            exit 1
            ;;
    esac
}
DISTRO="${1:-$(detect_distro)}"
validate_distro "${DISTRO}"

id
if [ "$(id -u)" = "0" ]; then
    echo "Should not be run as root"
    exit 1
fi

env
if [ -n "${DAOS_DEPS_EXT_REPO:-}" ]; then
    sudo tee /etc/yum.repos.d/daos-deps-extra.repo > /dev/null <<-EOF
	[daos-deps-extra]
	name=DAOS dependency RPMs extra repo
	baseurl=${DAOS_DEPS_EXT_REPO}
	enabled=1
	gpgcheck=0
	EOF
    trap 'sudo rm -f /etc/yum.repos.d/daos-deps-extra.repo' EXIT
fi

export DISTRO
libfabric_pkg="$("${rpms_dir}/package_version.sh" libfabric dev)"
mercury_pkg="$("${rpms_dir}/package_version.sh" mercury dev)"
argobots_pkg="$("${rpms_dir}/package_version.sh" argobots dev)"
fused_pkg="$("${rpms_dir}/package_version.sh" fused dev)"
isal_pkg="$("${rpms_dir}/package_version.sh" isal dev)"
isal_crypto_pkg="$("${rpms_dir}/package_version.sh" isal_crypto dev)"
daos_spdk_pkg="$("${rpms_dir}/package_version.sh" daos_spdk dev)"
pmdk_pkg="$("${rpms_dir}/package_version.sh" pmdk dev pmemobj)"

sudo dnf install --allowerasing -y "${libfabric_pkg}" || echo "${libfabric_pkg} not available"
sudo dnf install --allowerasing -y "${mercury_pkg}" || echo "${mercury_pkg} not available"
sudo dnf install --allowerasing -y "${argobots_pkg}" || echo "${argobots_pkg} not available"
sudo dnf install --allowerasing -y "${daos_spdk_pkg}" || echo "${daos_spdk_pkg} not available"
sudo dnf install --allowerasing -y "${fused_pkg}" || echo "${fused_pkg} not available"
sudo dnf install --allowerasing -y "${pmdk_pkg}" || echo "${pmdk_pkg} not available"
sudo dnf install --allowerasing -y "${isal_pkg}" || echo "${isal_pkg} not available"
sudo dnf install --allowerasing -y "${isal_crypto_pkg}" || echo "${isal_crypto_pkg} not available"
