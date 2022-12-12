#!/usr/bin/env bash
# shellcheck shell=bash
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
#
# Install DAOS Client package
#
DAOS_VERSION="${DAOS_VERSION:-2.2}"

set_vars() {
  # shellcheck disable=SC1091
  source "/etc/os-release"
  OS_VERSION_ID="${ID,,}_${VERSION_ID}"
  OS_MAJOR_VERSION=$(echo "${VERSION_ID}" | cut -d. -f1)
  OS_MAJOR_VERSION_ID="${ID,,}_${OS_MAJOR_VERSION}"

  case "${OS_MAJOR_VERSION_ID}" in
    centos_7)
      DAOS_OS_VERSION="CentOS7"
      PKG_MGR="yum"
      REPO_PATH=/etc/yum.repos.d
      ;;
    almalinux_8|centos_8|rhel_8|rocky_8)
      DAOS_OS_VERSION="EL8"
      PKG_MGR="dnf"
      REPO_PATH=/etc/yum.repos.d
      ;;
    opensuse-leap_15)
      if [[ "${OS_VERSION_ID}" == "opensuse-leap_15.4" ]]; then
        log.error "Unsupported OS: ${OS_VERSION_ID}."
        log.error "See https://daosio.atlassian.net/browse/DAOS-11637"
        exit 1
      fi
      DAOS_OS_VERSION="Leap15"
      PKG_MGR="zypper"
      REPO_PATH=/etc/zypp/repos.d
      ;;
    *)
      log.error "Unsupported OS: ${OS_VERSION_ID}. Exiting."
      exit 1
      ;;
  esac
}

install_epel() {
  # DAOS has dependencies on packages in epel
  if [[ "${ID}" != "opensuse-leap" ]]; then
    if rpm -qa | grep -q "epel-release"; then
      echo "epel-release already installed"
    else
      echo "Installing epel-release"
      $PKG_MGR install -y "https://dl.fedoraproject.org/pub/epel/epel-release-latest-${OS_MAJOR_VERSION}.noarch.rpm"
      $PKG_MGR upgrade -y epel-release
    fi
  fi
}

add_daos_repo() {
  local repo_file="${REPO_PATH}/daos.repo"
  rm -f "${repo_file}"
  echo "Adding DAOS v${DAOS_VERSION} packages repo"
  curl -s -k --output "${repo_file}" "https://packages.daos.io/v${DAOS_VERSION}/${DAOS_OS_VERSION}/packages/x86_64/daos_packages.repo"
  if [[ "${OS_VERSION_ID}" == "opensuse-leap_15" ]]; then
    sed -i 's|gpgkey=.*|gpgkey=https://packages.daos.io/RPM-GPG-KEY|g' "${repo_file}"
  fi
}

install_daos_client() {
    "${PKG_MGR}" install -y daos-client daos-devel
    # Disable daos_agent service.
    # It will be enabled by a startup script after the service has been configured.
    systemctl disable daos_agent
}

main() {
  set_vars
  install_epel
  add_daos_repo
  install_daos_client
}

main
