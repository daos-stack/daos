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

#
# Build daos-server and daos-client images.
# The daos-client image will have IO500 pre-installed.
#
set -e

trap cleanup EXIT
trap 'printf "\n\n%s\n\n" "Terminating script ... "; cleanup' SIGINT
trap 'printf "\n\n%s\n\n" "Hit an unexpected and unchecked error. Exiting."; cleanup' ERR

SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )"

# CLIENT_OS_FAMILY="daos-client-hpc-centos-7"
# SERVER_OS_FAMILY="daos-server-centos-7"

CLIENT_OS_FAMILY="daos-client-io500-hpc-centos-7"
SERVER_OS_FAMILY="daos-server-io500-centos-7"

IO500_INSTALL_SCRIPTS_DIR="${SCRIPT_DIR}/install_scripts"

# Ordered list of scripts in $IO500_INSTALL_SCRIPTS_DIR that should be run by
# packer when building the DAOS client image with IO500 pre-installed.
INSTALL_SCRIPTS=(
install_devtools.sh
install_intel-oneapi.sh
install_mpifileutils.sh
install_io500-isc22.sh
)

# Reverse the INSTALL_SCRIPTS array
last=${#INSTALL_SCRIPTS[@]}
declare -a INSTALL_SCRIPTS_REVERSED
for (( i=last-1 ; i>=0 ; i-- )); do
  INSTALL_SCRIPTS_REVERSED+=("${INSTALL_SCRIPTS[i]}")
done

IMAGES_DIR="${SCRIPT_DIR}/../../../images"
FORCE_REBUILD="${FORCE_REBUILD:-0}"
ERROR_MSGS=()

show_help() {
  cat <<EOF

Usage:

  ${SCRIPT_NAME} <options>

Options:

  Required

    -t --image-type DAOS_INSTALL_TYPE  Image Type
                                Valid values [ all | client | server ]

  Optional

    -p --project  GCP_PROJECT  Google Cloud Platform Project ID
    -r --region   GCP_REGION   Google Cloud Platform Compute Region
    -z --zone     GCP_ZONE     Google Cloud Platform Compute Zone

    -v --version  DAOS_VERSION Version of DAOS to install from
                               https://packages.daos.io/
                               If https://packages.daos.io/v2.0.0
                               then --version "2.0.0"

    -u --repo-baseurl DAOS_REPO_BASE_URL
                               Base URL of a repo.
                               This is the URL up to the version.
                               If the repo is at https://example.com/foo/v${DAOS_VERSION}
                               then -u "https://example.com/foo"

    -f --force                 Force images to be built if there are already
                               existing images

    -h --help                  Show help

Examples:

  Build daos-client-io500 images
    ${SCRIPT_NAME} -t client

  Build daos-server images
    ${SCRIPT_NAME} -t server

  Build both daos-server and daos-client images
    ${SCRIPT_NAME} -t all

Dependencies:

  ${SCRIPT_NAME} uses the Google Cloud Platform SDK (gcloud command)

  You must install the Google Cloud SDK and make sure it is in your PATH
  See https://cloud.google.com/sdk/docs/install

EOF
}

log() {
  local msg
  local print_lines
  msg="$1"
  print_lines="$2"
  # shellcheck disable=SC2155,SC2183
  local line=$(printf "%80s" | tr " " "-")
  if [[ -t 1 ]]; then tput setaf 14; fi
  if [[ "${print_lines}" == 1 ]]; then
    printf -- "\n%s\n %-78s \n%s\n" "${line}" "${msg}" "${line}"
  else
    printf -- "\n%s\n\n" "${msg}"
  fi
  if [[ -t 1 ]]; then tput sgr0; fi
}

log_error() {
  # shellcheck disable=SC2155,SC2183
  if [[ -t 1 ]]; then tput setaf 160; fi
  printf -- "\n%s\n\n" "${1}" >&2;
  if [[ -t 1 ]]; then tput sgr0; fi
}

log_section() {
  log "$1" "1"
}

verify_cloudsdk() {
  local gcloud_path
  gcloud_path="$(which gcloud)"
  if [[ -z "${gcloud_path}" ]]; then
    log_error "ERROR: gcloud not found"
    log_error "       Is the Google Cloud Platform SDK installed?"
    log_error "       See https://cloud.google.com/sdk/docs/install"
    show_help
    exit 1
  fi
}

check_existing_builds() {
  if [[ "${DAOS_INSTALL_TYPE}" =~ ^(all|server)$ ]]; then
    SERVER_IMAGE=$(gcloud compute images list --project="${GCP_PROJECT}" --format='value(name)' --filter="name:${SERVER_OS_FAMILY}*" --limit=1)
    if [[ "${FORCE_REBUILD}" -eq 1 ]] || [[ -z ${SERVER_IMAGE} ]]; then
      export BUILD_SERVER_IMAGE=1
    else
      log "Server image '${SERVER_IMAGE}' exists. Skipping build."
      export BUILD_SERVER_IMAGE=0
    fi
  fi

  if [[ "${DAOS_INSTALL_TYPE}" =~ ^(all|client)$ ]]; then
    CLIENT_IMAGE=$(gcloud compute images list --project="${GCP_PROJECT}" --format='value(name)' --filter="name:${CLIENT_OS_FAMILY}*" --limit=1)
    if [[ "${FORCE_REBUILD}" -eq 1 ]] || [[ -z ${CLIENT_IMAGE} ]]; then
      export BUILD_CLIENT_IMAGE=1
    else
      log "Client image '${CLIENT_IMAGE}' exists. Skipping build."
      export BUILD_CLIENT_IMAGE=0
    fi
  fi

  if [[ ${BUILD_SERVER_IMAGE} -eq 0 ]] && [[ ${BUILD_CLIENT_IMAGE} -eq 0 ]]; then
    exit 0
  fi
}

create_tmp_dir() {
  TMP_DIR="$(mktemp -d)"
  log "Temp directory for image building: ${TMP_DIR}"
  cp -r "${IMAGES_DIR}" "${TMP_DIR}/"
  TMP_IMAGES_DIR="${TMP_DIR}/$(basename "${IMAGES_DIR}")"
  TMP_SCRIPTS_DIR="${TMP_IMAGES_DIR}/scripts"
  TMP_CLIENT_PACKER_FILE="${TMP_IMAGES_DIR}/daos-client-image.pkr.hcl"
  TMP_SERVER_PACKER_FILE="${TMP_IMAGES_DIR}/daos-server-image.pkr.hcl"
}

cleanup() {
  if [[ -n "${TMP_DIR}" ]]; then
    if [[ -d "${TMP_DIR}" ]]; then
      rm -r "${TMP_DIR}"
    fi
  fi
}

add_script() {
  script_name="$1"
  comma="$2"
  if ! grep -q "${script_name}" "${TMP_CLIENT_PACKER_FILE}"; then
    sed -i "\|\"./scripts/install_daos.sh\",$|a \\      \"./scripts/${script_name}\"${comma}" "${TMP_CLIENT_PACKER_FILE}"
  fi
}

add_install_scripts() {

  last=${#INSTALL_SCRIPTS_REVERSED[@]}

  if [[ ${last} -gt 0 ]]; then
    # Update the daos-client-image.json packer file to add additional scripts to run
    # when building the image.
    # Look for "./scripts/install_daos.sh" and add more scripts after that line
    sed -i 's/install_daos.sh"$/install_daos.sh",/g' "${TMP_CLIENT_PACKER_FILE}"

    for (( i=0 ; i<last ; i++ ));do
      comma=","
      if (( i == 0 )); then comma=""; fi
      script="${INSTALL_SCRIPTS_REVERSED[i]}"
      add_script "${script}" "${comma}"

      # Copy the script to the images/scripts directory
      cp "${IO500_INSTALL_SCRIPTS_DIR}/${script}" "${TMP_SCRIPTS_DIR}/${script}"
    done
  fi
}

modify_image_names() {
  # Modify daos-client image name and image family to include 'io500' in the name
  sed -i "s/daos-client-hpc-centos-7/${CLIENT_OS_FAMILY}/g" "${TMP_CLIENT_PACKER_FILE}"
  sed -i "s/daos-server-centos-7/${SERVER_OS_FAMILY}/g" "${TMP_SERVER_PACKER_FILE}"
}

build_images() {
  # Build the DAOS disk images if they don't exist in the project or if the
  # -f | --force option was passed.

  cd "${TMP_IMAGES_DIR}"

  if [[ "${BUILD_SERVER_IMAGE}" -eq 1 ]]; then
      log "Building DAOS server image: ${SERVER_OS_FAMILY}"
      ./build_images.sh -t "server"
  fi

  if [[ "${BUILD_CLIENT_IMAGE}" -eq 1 ]]; then
      log "Building DAOS client image: ${CLIENT_OS_FAMILY}"
      ./build_images.sh -t "client"
  fi
}

show_errors() {
  # If there are errors, print the error messages and exit
  if [[ ${#ERROR_MSGS[@]} -gt 0 ]]; then
    printf "\n" >&2
    log_error "${ERROR_MSGS[@]}"
    show_help
    exit 1
  fi
}

opts() {

  if [[ "$#" -eq 0 ]]; then
    show_help
    exit 0
  fi

  verify_cloudsdk

  # shift will cause the script to exit if attempting to shift beyond the
  # max args.  So set +e to continue processing when shift errors.
  set +e
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --type|-t)
        DAOS_INSTALL_TYPE="$2"
        if [[ "${DAOS_INSTALL_TYPE}" == -* ]] || [[ "${DAOS_INSTALL_TYPE}" = "" ]] || [[ -z ${DAOS_INSTALL_TYPE} ]]; then
          ERROR_MSGS+=("ERROR: Missing DAOS_INSTALL_TYPE value for -t or --type")
          break
        elif [[ ! "${DAOS_INSTALL_TYPE}" =~ ^(all|server|client)$ ]]; then
          ERROR_MSGS+=("ERROR: Invalid value '${DAOS_INSTALL_TYPE}' for DAOS_INSTALL_TYPE")
          ERROR_MSGS+=("       Valid values are 'all', 'server', 'client'")
        fi
        shift 2
      ;;
      --project|-p)
        GCP_PROJECT="$2"
        if [[ "${GCP_PROJECT}" == -* ]] || [[ "${GCP_PROJECT}" = "" ]] || [[ -z ${GCP_PROJECT} ]]; then
          ERROR_MSGS+=("ERROR: Missing GCP_PROJECT value for -p or --project")
          break
        fi
        shift 2
      ;;
      --region|-r)
        GCP_REGION="$2"
        if [[ "${GCP_REGION}" == -* ]] || [[ "${GCP_REGION}" = "" ]] || [[ -z ${GCP_REGION} ]]; then
          ERROR_MSGS+=("ERROR: Missing GCP_REGION value for -r or --region")
          break
        fi
        shift 2
      ;;
      --zone|-z)
        GCP_ZONE="$2"
        if [[ "${GCP_ZONE}" == -* ]] || [[ "${GCP_ZONE}" = "" ]] || [[ -z ${GCP_ZONE} ]]; then
          ERROR_MSGS+=("ERROR: Missing GCP_ZONE value for -z or --zone")
          break
        fi
        shift 2
      ;;
      --repo-baseurl|-u)
        DAOS_REPO_BASE_URL="${2}"
        if [[ "${DAOS_REPO_BASE_URL}" == -* ]] || [[ "${DAOS_REPO_BASE_URL}" = "" ]] || [[ -z ${DAOS_REPO_BASE_URL} ]]; then
          log_error "ERROR: Missing URL value for --repo-baseurl"
          show_help
          exit 1
        fi
        shift 2
      ;;
      --force|-f)
        FORCE_REBUILD=1
        shift
      ;;
      --version|-v)
        DAOS_VERSION="${2}"
        export DAOS_VERSION
        shift 2
      ;;
      --help|-h)
        show_help
        exit 0
      ;;
	    --*|-*)
        ERROR_MSGS+=("ERROR: Unrecognized option '${1}'")
        shift
      ;;
	    *)
        ERROR_MSGS+=("ERROR: Unrecognized option '${1}'")
        shift
        break
      ;;
    esac
  done
  set -e

  # Before we attempt to do lookups for project, region, and zone show the
  # errors and exit if there are any errors at this point.
  show_errors

  GCP_PROJECT="${GCP_PROJECT:-"${GCP_PROJECT}"}"
  GCP_PROJECT="${GCP_PROJECT:-"${CLOUDSDK_PROJECT}"}"
  GCP_PROJECT="${GCP_PROJECT:-$(gcloud config list --format='value(core.project)')}"
  [[ -z ${GCP_PROJECT} ]] && ERROR_MSGS+=("ERROR: core.project value not found in Cloud SDK configuration and no value passed for --project")

  GCP_REGION="${GCP_REGION:-"${GCP_REGION}"}"
  GCP_REGION="${GCP_REGION:-"${CLOUDSDK_REGION}"}"
  GCP_REGION="${GCP_REGION:-$(gcloud config list --format='value(compute.region)')}"
  [[ -z ${GCP_REGION} ]] && ERROR_MSGS+=("ERROR: compute.region value not found in Cloud SDK configuration and no value passed for --region")

  GCP_ZONE="${GCP_ZONE:-"${GCP_ZONE}"}"
  GCP_ZONE="${GCP_ZONE:-"${CLOUDSDK_ZONE}"}"
  GCP_ZONE="${GCP_ZONE:-$(gcloud config list --format='value(compute.zone)')}"
  [[ -z ${GCP_ZONE} ]] && ERROR_MSGS+=("ERROR: compute.zone value not found in Cloud SDK configuration and no value passed for --zone")


  # Now that we've checked all other variables, exit if there are any errors.
  show_errors

  export GCP_PROJECT
  export GCP_REGION
  export GCP_ZONE
  export DAOS_INSTALL_TYPE
  export FORCE_REBUILD
}

main() {
  log_section "Building DAOS disk images for IO500"
  opts "$@"
  check_existing_builds
  create_tmp_dir
  modify_image_names
  add_install_scripts
  build_images
  cleanup
}

main "$@"
