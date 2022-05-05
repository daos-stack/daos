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
# Build DAOS server and client images using Packer in Google Cloud Build
#

set -e
trap 'echo "Unexpected and unchecked error. Exiting."' ERR

SCRIPT_NAME=$(basename "$0")
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )"
START_TIMESTAMP=$(date "+%FT%T")

# Set the default DAOS_VERSION
source "${SCRIPT_DIR}/daos_version.sh"

DAOS_VERSION="${DAOS_VERSION:-${DEFAULT_DAOS_VERSION}}"
DAOS_REPO_BASE_URL="${DAOS_REPO_BASE_URL:-${DEFAULT_DAOS_REPO_BASE_URL}}"
FORCE_REBUILD=0
ERROR_MSGS=()

show_help() {
   cat <<EOF

Usage:

  ${SCRIPT_NAME} <options>

  Build DAOS Server and Client images

Options:

  -t --type           DAOS_INSTALL_TYPE    Installation Type
                                           Valid values [ all | client | server ]

  [ -v --version      DAOS_VERSION ]       Version of DAOS to install

  [ -u --repo-baseurl DAOS_REPO_BASE_URL ] Base URL of a repo

  [ -p --project      GCP_PROJECT ]        Google Cloud Platform Project ID
                                           Default: Cloud SDK default project

  [ -z --zone         GCP_ZONE    ]        Google Cloud Platform Compute Zone
                                           Default: Cloud SDK default zone

  [ -h --help ]                     Show help

Examples:

  Build daos-client image with DAOS v${DAOS_VERSION} installed

    ${SCRIPT_NAME} -t client

  Build daos-server image with DAOS v${DAOS_VERSION} installed

    ${SCRIPT_NAME} -t server

  Build daos-client and daos-server images with DAOS v${DAOS_VERSION} installed

    ${SCRIPT_NAME} -t all

EOF
}

log() {
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
  printf -- "%s\n" "${1}" >&2;
  if [[ -t 1 ]]; then tput sgr0; fi
}

log_section() {
  log "$1" "1"
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

verify_cloudsdk() {
  if ! gcloud -v &> /dev/null; then
    log_error "ERROR: gcloud not found
       Is the Google Cloud Platform SDK installed?
       See https://cloud.google.com/sdk/docs/install"
    exit 1
  fi
}

opts() {

  # Show help if no options were provided
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
      --version|-v)
        DAOS_VERSION="${2}"
        if [[ "${DAOS_VERSION}" == -* ]] || [[ "${DAOS_VERSION}" = "" ]] || [[ -z ${DAOS_VERSION} ]]; then
          log_error "ERROR: Missing VERSION value for -v or --version"
          show_help
          exit 1
        else
          # Verify that it looks like a version number
          if ! echo "${DAOS_VERSION}" | grep -q -E "([0-9]{1,}\.)+[0-9]{1,}"; then
            log_error "ERROR: Value '${DAOS_VERSION}' for -v or --version does not appear to be a valid version"
            show_help
            exit 1
          fi
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
      --project|-p)
        GCP_PROJECT="$2"
        if [[ "${GCP_PROJECT}" == -* ]] || [[ "${GCP_PROJECT}" = "" ]] || [[ -z ${GCP_PROJECT} ]]; then
          ERROR_MSGS+=("ERROR: Missing GCP_PROJECT value for -p or --project")
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
      --force|-f)
        FORCE_REBUILD=1
        shift
      ;;
      --help|-h)
        show_help
        exit 0
      ;;
      --)
        break
      ;;
	    --*|-*)
        log_error "ERROR: Unrecognized option '${1}'"
        show_help
        exit 1
      ;;
	    *)
        log_error "ERROR: Unrecognized option '${1}'"
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
  if [[ -z ${GCP_PROJECT} ]]; then
    ERROR_MSGS+=("ERROR: core.project value not found in Cloud SDK configuration and no value passed for --project")
    ERROR_MSGS+=("       Set your default project with 'gcloud config set project <project_id>'")
  fi

  GCP_ZONE="${GCP_ZONE:-"${GCP_ZONE}"}"
  GCP_ZONE="${GCP_ZONE:-"${CLOUDSDK_ZONE}"}"
  GCP_ZONE="${GCP_ZONE:-$(gcloud config list --format='value(compute.zone)')}"
  if [[ -z ${GCP_ZONE} ]]; then
    ERROR_MSGS+=("ERROR: compute.zone value not found in Cloud SDK configuration and no value passed for --zone")
    ERROR_MSGS+=("       Set your default zone with 'gcloud config set compute/zone <zone>'")
  fi

  # Now that we've checked all other variables, exit if there are any errors.
  show_errors

  export GCP_PROJECT
  export GCP_ZONE
  export DAOS_INSTALL_TYPE
  export DAOS_VERSION
  export DAOS_REPO_BASE_URL
  export FORCE_REBUILD

}


configure_gcp_project() {

  # The service account used here should have been already created
  # by the "packer_build" step.  We are just checking here.
  CLOUD_BUILD_ACCOUNT=$(gcloud projects get-iam-policy "${GCP_PROJECT}" \
  --filter="(bindings.role:roles/cloudbuild.builds.builder)"  \
  --flatten="bindings[].members" \
  --format="value(bindings.members[])" \
  --limit=1)
  log "Packer will be using service account ${CLOUD_BUILD_ACCOUNT}"

  # Add cloudbuild SA permissions
  CHECK_ROLE_INST_ADMIN=$(
    gcloud projects get-iam-policy "${GCP_PROJECT}" \
    --flatten="bindings[].members" \
    --filter="bindings.role=roles/compute.instanceAdmin.v1 AND \
              bindings.members=${CLOUD_BUILD_ACCOUNT}" \
    --format="value(bindings.members[])"
  )
  if [[ "${CHECK_ROLE_INST_ADMIN}" != "${CLOUD_BUILD_ACCOUNT}" ]]; then
    gcloud projects add-iam-policy-binding "${GCP_PROJECT}" \
      --member "${CLOUD_BUILD_ACCOUNT}" \
      --role roles/compute.instanceAdmin.v1
  fi

  CHECK_ROLE_SVC_ACCT=$(
    gcloud projects get-iam-policy "${GCP_PROJECT}" \
    --flatten="bindings[].members" \
    --filter="bindings.role=roles/iam.serviceAccountUser AND \
              bindings.members=${CLOUD_BUILD_ACCOUNT}" \
    --format="value(bindings.members[])"
  )
  if [[ "${CHECK_ROLE_SVC_ACCT}" != "${CLOUD_BUILD_ACCOUNT}" ]]; then
    gcloud projects add-iam-policy-binding "${GCP_PROJECT}" \
      --member "${CLOUD_BUILD_ACCOUNT}" \
      --role roles/iam.serviceAccountUser
  fi

  CHECK_ROLE_IAP_TUNL_RESR_ACCS=$(
    gcloud projects get-iam-policy "${GCP_PROJECT}" \
    --flatten="bindings[].members" \
    --filter="bindings.role=roles/iap.tunnelResourceAccessor AND \
              bindings.members=${CLOUD_BUILD_ACCOUNT}" \
    --format="value(bindings.members[])"
  )
  if [[ "${CHECK_ROLE_IAP_TUNL_RESR_ACCS}" != "${CLOUD_BUILD_ACCOUNT}" ]]; then
    gcloud projects add-iam-policy-binding "${GCP_PROJECT}" \
      --member "${CLOUD_BUILD_ACCOUNT}" \
      --role roles/iap.tunnelResourceAccessor
  fi

}

build_images() {
  # Increase timeout to 1hr to make sure we don't time out
  if [[ "${DAOS_INSTALL_TYPE}" =~ ^(all|server)$ ]]; then
    log "Building server image"
    gcloud builds submit --timeout=1800s \
    --substitutions="_PROJECT_ID=${GCP_PROJECT},_ZONE=${GCP_ZONE},_DAOS_VERSION=${DAOS_VERSION},_DAOS_REPO_BASE_URL=${DAOS_REPO_BASE_URL}" \
    --config=packer_cloudbuild-server.yaml .
  fi

  if [[ "${DAOS_INSTALL_TYPE}" =~ ^(all|client)$ ]]; then
    log "Building client image"
    gcloud builds submit --timeout=1800s \
    --substitutions="_PROJECT_ID=${GCP_PROJECT},_ZONE=${GCP_ZONE},_DAOS_VERSION=${DAOS_VERSION},_DAOS_REPO_BASE_URL=${DAOS_REPO_BASE_URL}" \
    --config=packer_cloudbuild-client.yaml .
  fi
}

list_images() {
  log "Image(s) created"
  gcloud compute images list \
    --project="${GCP_PROJECT}" \
    --filter="name:daos-* AND creationTimestamp>=${START_TIMESTAMP}" \
    --format="table(name,family,creationTimestamp)" \
    --sort-by="creationTimestamp"
}

main() {
  opts "$@"
  verify_cloudsdk
  log_section "Building DAOS Image(s)"
  configure_gcp_project
  build_images
  list_images
}

main "$@"
