#!/bin/bash
# Copyright 2022 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Install DAOS Client package
#
# TODO: Add support for installing on openSUSE Leap 15.3 and Ubuntu 20.04 LTS
#

DAOS_VERSION="${DAOS_VERSION:-2.0}"

echo "BEGIN: DAOS Client Installation"

# Determine which repo to use
# shellcheck disable=SC1091
source "/etc/os-release"
OS_VERSION=$(echo "${VERSION_ID}" | cut -d. -f1)
OS_VERSION_ID="${ID,,}_${OS_VERSION}"
case ${OS_VERSION_ID} in
  centos_7)
    DAOS_OS_VERSION="CentOS7"
    ;;
  centos_8)
    DAOS_OS_VERSION="CentOS8"
    ;;
  rocky_8)
    DAOS_OS_VERSION="CentOS8"
    ;;
  *)
    echo "ERROR: Unsupported OS: ${OS_VERSION_ID}. Exiting."
    exit 1
    ;;
esac

echo "Adding DAOS v${DAOS_VERSION} packages repo"
curl -s --output /etc/yum.repos.d/daos_packages.repo "https://packages.daos.io/v${DAOS_VERSION}/${DAOS_OS_VERSION}/packages/x86_64/daos_packages.repo"

echo "Installing daos-client and daos-devel packages"
yum install -y daos-client daos-devel
systemctl enable daos_agent

echo "END: DAOS Client Installation"
