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
# File: cloudshell_urls.sh
#
# Description:
#   This script will update "Open in Google Cloud Shell" in all *.md files.
#   Before merging from the develop branch to main run
#
#     ./cloudshell_urls.sh -b main -r https://github.com/daos-stack/google-cloud-daos
#

set -e
trap 'echo "Unexpected and unchecked error. Exiting."' ERR

SCRIPT_NAME=$(basename "$0")
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )"

CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
CURRENT_REMOTE_URL=$(git remote get-url $(git for-each-ref --format='%(upstream:short)' $(git symbolic-ref -q HEAD)|cut -d/ -f1) | sed 's|git@github.com:|https://github.com/|g' | sed 's|\.git||g')

show_help() {
   cat <<EOF

Usage:

  ${SCRIPT_NAME} <options>

  Update all "Open in Google Cloud Shell" links in *.md files

Options:

  [-b --branch]     The branch that is set in the link.
                    If not provided the default branch "${BRANCH}" is used.

  [-r --repo-url]   The repository URL that is set in the link.
                    If not provided the URL of the origin repo "${REMOTE_URL}" is used.

  [ -h --help ]     Show help

Examples:

  Set "Open in Google Cloud Shell" links before merging to main

    ${SCRIPT_NAME} --branch main --repo-url https://github.com/daos-stack/google-cloud-daos

  Set "Open in Google Cloud Shell" links when submitting a PR to the develop branch

    ${SCRIPT_NAME} --branch develop --repo-url https://github.com/daos-stack/google-cloud-daos

EOF
}


opts() {
  # shift will cause the script to exit if attempting to shift beyond the
  # max args.  So set +e to continue processing when shift errors.
  set +e
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --branch|-b)
        BRANCH="$2"
        shift 2
      ;;
      --repo-url|-r)
        REMOTE_URL="${2}"
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

  if [[ "${BRANCH}" = "" ]] || [[ -z ${BRANCH} ]]; then
    BRANCH="${CURRENT_BRANCH}"
  fi

  if [[ "${REMOTE_URL}" = "" ]] || [[ -z ${REMOTE_URL} ]]; then
    REMOTE_URL="${CURRENT_REMOTE_URL}"
  fi
}

update_cloud_shell_urls() {
  #for mdf in $(find "${SCRIPT_DIR}/../../" -type f -name "*.md"); do
  for mdf in $(grep -R -l --include "*.md" "https://console.cloud.google.com/cloudshell/open" "${SCRIPT_DIR}/../.."); do
    log "Updating file: ${mdf}"

    sed -r -i "s|git_repo=[^\&]*\&|git_repo=${REMOTE_URL}\&|g" "${mdf}"
    sed -r -i "s|cloudshell_git_branch=[^\&]*\&|cloudshell_git_branch=${BRANCH}\&|g" "${mdf}"
  done

  grep -R --include "*.md" "https://console.cloud.google.com/cloudshell/open" "${SCRIPT_DIR}/../.."
}

log_start() {
  # shellcheck disable=SC2155,SC2183
  local line=$(printf "%80s" | tr " " "-")
  if [[ -t 1 ]]; then tput setaf 14; fi
  printf -- "\n%s\n" "${line}"
  printf "%-78s\n\n" "Updating 'Open in Google Cloud Shell' links in *.md files"
  printf "git_repo=%-78s\n" "${REMOTE_URL}"
  printf "cloudshell_git_branch=%-78s\n" "${BRANCH}"
  printf -- "%s\n" "${line}"
  if [[ -t 1 ]]; then tput sgr0; fi
}

log() {
  # shellcheck disable=SC2155,SC2183
  local line=$(printf "%80s" | tr " " "-")
  if [[ -t 1 ]]; then tput setaf 15; fi
  printf "%-78s\n" "${1}"
  if [[ -t 1 ]]; then tput sgr0; fi
}



main() {
  opts "$@"
  log_start
  update_cloud_shell_urls
}

main "$@"
