#!/usr/bin/env bash
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
# Install DAOS Server or Client packages
#

set -eo pipefail
trap "echo 'An unexpected error occurred. Exiting.'" ERR

SCRIPT_FILENAME=$(basename "${BASH_SOURCE[0]}")
DAOS_REPO_BASE_URL="${DAOS_REPO_BASE_URL:-https://packages.daos.io}"

# BEGIN: Logging variables and functions
declare -A LOG_LEVELS=([DEBUG]=0 [INFO]=1  [WARN]=2   [ERROR]=3 [FATAL]=4 [OFF]=5)
declare -A LOG_COLORS=([DEBUG]=2 [INFO]=12 [WARN]=3 [ERROR]=1 [FATAL]=9 [OFF]=0 [OTHER]=15)
LOG_LEVEL=INFO

log() {
  local msg="$1"
  local lvl=${2:-INFO}
  if [[ ${LOG_LEVELS[$LOG_LEVEL]} -le ${LOG_LEVELS[$lvl]} ]]; then
    if [[ -t 1 ]]; then tput setaf "${LOG_COLORS[$lvl]}"; fi
    printf "[%-5s] %s\n" "$lvl" "${msg}" 1>&2
    if [[ -t 1 ]]; then tput sgr0; fi
  fi
}

log.debug() { log "${1}" "DEBUG" ; }
log.info()  { log "${1}" "INFO"  ; }
log.warn()  { log "${1}" "WARN"  ; }
log.error() { log "${1}" "ERROR" ; }
log.fatal() { log "${1}" "FATAL" ; }
# END: Logging variables and functions

show_help() {
  echo "
Usage:

  ${SCRIPT_FILENAME} <options>

  Install the DAOS server or client

Options:

  -t --type     DAOS_INSTALL_TYPE        Installation Type
                                         Valid values [ all | client | server | admin ]

  -v --version  DAOS_VERSION              Version of DAOS to install

  [-u --repo-baseurl DAOS_REPO_BASE_URL ] Base URL of a repo

  [ -h --help ]                           Show help

Examples:

  Install daos-admin
    ${SCRIPT_FILENAME} -t admin

  Install daos-client

    ${SCRIPT_FILENAME} -t client

  Install daos-server

    ${SCRIPT_FILENAME} -t server

"
}

opts() {
  # shift will cause the script to exit if attempting to shift beyond the
  # max args.  So set +e to continue processing when shift errors.
  set +e
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --type|-t)
        DAOS_INSTALL_TYPE="$2"
        if [[ "${DAOS_INSTALL_TYPE}" == -* ]] || [[ "${DAOS_INSTALL_TYPE}" = "" ]] || [[ -z ${DAOS_INSTALL_TYPE} ]]; then
          log.error "Missing INSTALL_TYPE value for -t or --type"
          show_help
          exit 1
        elif [[ ! "${DAOS_INSTALL_TYPE}" =~ ^(all|server|client)$ ]]; then
          log.error "Invalid value '${DAOS_INSTALL_TYPE}' for INSTALL_TYPE"
          log.error "Valid values are 'all', 'server', 'client', 'admin'"
          show_help
          exit 1
        fi
        shift 2
      ;;
      --version|-v)
        DAOS_VERSION="${2}"
        if [[ "${DAOS_VERSION}" == -* ]] || [[ "${DAOS_VERSION}" = "" ]] || [[ -z ${DAOS_VERSION} ]]; then
          log.error "Missing VERSION value for -v or --version"
          show_help
          exit 1
        else
          # Verify that it looks like a version number
          if ! echo "${DAOS_VERSION}" | grep -q -E "([0-9]{1,}\.)+[0-9]{1,}"; then
            log.error "Value '${DAOS_VERSION}' for -v or --version does not appear to be a valid version"
            show_help
            exit 1
          fi
        fi
        shift 2
      ;;
      --repo-baseurl|-u)
        DAOS_REPO_BASE_URL="${2}"
        if [[ "${DAOS_REPO_BASE_URL}" == -* ]] || [[ "${DAOS_REPO_BASE_URL}" = "" ]] || [[ -z ${DAOS_REPO_BASE_URL} ]]; then
          log.error "Missing URL value for --repo-baseurl"
          show_help
          exit 1
        fi
        shift 2
      ;;
      --help|-h)
        show_help
        exit 0
      ;;
      --)
        break
      ;;
	    --*|-*)
        log.error "Unrecognized option '${1}'"
        show_help
        exit 1
      ;;
	    *)
        log.error "Unrecognized option '${1}'"
        shift
        break
      ;;
    esac
  done
  set -eo pipefail

  if [[ -z ${DAOS_INSTALL_TYPE} ]]; then
    log.error "-t INSTALL_TYPE required"
    show_help
    exit 1
  fi

  if [[ -z ${DAOS_VERSION} ]]; then
    log.error "-v VERSION required"
    show_help
    exit 1
  fi

  export DAOS_INSTALL_TYPE
  export DAOS_VERSION
  export DAOS_REPO_BASE_URL
}

set_os_specific_vars() {
  # shellcheck disable=SC1091
  source "/etc/os-release"
  OS_VERSION_ID="${ID,,}_${VERSION_ID}"
  OS_MAJOR_VERSION=$(echo "${VERSION_ID}" | cut -d. -f1)
  OS_MAJOR_VERSION_ID="${ID,,}_${OS_MAJOR_VERSION}"

  log.debug "OS_VERSION_ID = ${OS_VERSION_ID}"
  log.debug "OS_MAJOR_VERSION = ${OS_MAJOR_VERSION}"
  log.debug "OS_MAJOR_VERSION_ID = ${OS_MAJOR_VERSION_ID}"

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

verify_version(){
  # Check to make sure the version exists
  local status_code=""
  status_code=$(curl -s -o /dev/null -w "%{http_code}" "${DAOS_REPO_BASE_URL}/v${DAOS_VERSION}")
  if [[ ! "${status_code}" =~ ^(200|301|302)$ ]]; then
    log.error "DAOS version '${DAOS_VERSION}' not found at ${DAOS_REPO_BASE_URL}/v${DAOS_VERSION}"
    exit 1
  fi
}

install_epel() {
  # DAOS has dependencies on packages in epel
  if [[ "${ID}" != "opensuse-leap" ]]; then
    if rpm -qa | grep -q "epel-release"; then
      log.info "epel-release already installed"
    else
      log.info "Installing epel-release"
      $PKG_MGR install -y "https://dl.fedoraproject.org/pub/epel/epel-release-latest-${OS_MAJOR_VERSION}.noarch.rpm"
      $PKG_MGR upgrade -y epel-release
    fi
    $PKG_MGR update -y
  fi
}

install_misc_pkgs() {
  local pkgs="clustershell curl git jq patch pdsh rsync wget"
  log.info "Installing packages: ${pkgs}"
  # shellcheck disable=SC2086
  "${PKG_MGR}" install -y ${pkgs}
}

add_daos_repo() {
  local repo_file="${REPO_PATH}/daos.repo"
  log.info "Adding DAOS v${DAOS_VERSION} packages repo"
  curl -s -k --output "${repo_file}" "https://packages.daos.io/v${DAOS_VERSION}/${DAOS_OS_VERSION}/packages/x86_64/daos_packages.repo"
  if [[ "${OS_VERSION_ID}" == "opensuse-leap_15" ]]; then
    log.debug "Fixing incorrect gpgkey setting in ${repo_file}"
    sed -i 's|gpgkey=.*|gpgkey=https://packages.daos.io/RPM-GPG-KEY|g' "${repo_file}"
  fi
}

install_daos() {
  if [[ "${DAOS_INSTALL_TYPE,,}" =~ ^(all|admin)$ ]]; then
    log.info "Install daos-admin package"
    $PKG_MGR install -y daos-admin
  fi

  if [[ "${DAOS_INSTALL_TYPE,,}" =~ ^(all|client)$ ]]; then
    log.info "Install daos-client and daos-devel packages"
    $PKG_MGR install -y daos-admin daos-client daos-devel
    # Disable daos_agent service.
    # It will be enabled by a startup script after the service has been configured.
    systemctl disable daos_agent
  fi

  if [[ "${DAOS_INSTALL_TYPE,,}" =~ ^(all|server)$ ]]; then
    log.info "Install daos-server packages"
    $PKG_MGR install -y daos-server daos-admin
    # Disable daos_server service.
    # It will be enabled by a startup script after the service has been configured.
    systemctl disable daos_server
  fi
}

main() {
  opts "$@"
  set_os_specific_vars
  log.info "Installing DAOS v${DAOS_VERSION}"
  verify_version
  install_epel
  install_misc_pkgs
  add_daos_repo
  install_daos
  log.info "DONE! DAOS v${DAOS_VERSION} installed"
}

main "$@"
