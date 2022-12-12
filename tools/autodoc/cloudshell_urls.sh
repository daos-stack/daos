#!/usr/bin/env bash
# shellcheck disable=SC2013
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

set -eo pipefail
trap 'echo "Unexpected and unchecked error. Exiting."' ERR

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" &>/dev/null && pwd -P)
SCRIPT_FILENAME=$(basename "${BASH_SOURCE[0]}")

# BEGIN: Logging variables and functions
declare -A LOG_LEVELS=([DEBUG]=0 [INFO]=1  [WARN]=2 [ERROR]=3 [FATAL]=4 [OFF]=5)
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
   cat <<EOF

Usage:

  ${SCRIPT_FILENAME} <options>

  Update all "Open in Google Cloud Shell" links in *.md files

Options:

  [-b --branch]     The branch that is set in the link.
                    If not provided the default branch "${BRANCH}" is used.

  [-r --repo-url]   The repository URL that is set in the link.
                    If not provided the URL of the origin repo "${REMOTE_URL}" is used.

  [ -h --help ]     Show help

Examples:

  Set "Open in Google Cloud Shell" links before merging to main

    ${SCRIPT_DIR}/${SCRIPT_FILENAME} --branch main --repo-url https://github.com/daos-stack/google-cloud-daos

  Set "Open in Google Cloud Shell" links when submitting a PR to the develop branch

    ${SCRIPT_DIR}/${SCRIPT_FILENAME} --branch develop --repo-url https://github.com/daos-stack/google-cloud-daos

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
        echo "ERROR: Unrecognized option '${1}'"
        show_help
        exit 1
      ;;
	    *)
        echo "ERROR: Unrecognized option '${1}'"
        shift
        break
      ;;
    esac
  done
  set -eo pipefail

  if [[ "${BRANCH}" == "" ]] || [[ -z ${BRANCH} ]]; then
    BRANCH="$(git rev-parse --abbrev-ref HEAD)"
  fi

  if [[ "${REMOTE_URL}" == "" ]] || [[ -z ${REMOTE_URL} ]]; then
    # shellcheck disable=SC2046
    REMOTE_URL=$(git config --get remote.origin.url | sed 's|git@github.com:|https://github.com/|g' | sed 's|\.git||g')
  fi
}

update_cloud_shell_urls() {
  for mdf in $(grep -R -l --include "*.md" \
                   --exclude "development.md" \
                   "https://ssh.cloud.google.com/cloudshell/open" \
                   "${REPO_DIR}"); do
    log.info "Updating cloud shell URLs in ${mdf}"
    sed -r -i "s|git_repo=[^\&]*\&|git_repo=${REMOTE_URL}.git\&|g" "${mdf}"
    sed -r -i "s|cloudshell_git_branch=[^\&]*\&|cloudshell_git_branch=${BRANCH}\&|g" "${mdf}"
    grep -o -E 'cloudshell_git_repo=.*cloudshell_git_branch=.*' "${mdf}" --color=auto
    echo
  done
  echo
}

update_git_urls() {
  for mdf in $(grep -R -l --include "*.md" \
                   --exclude "development.md" \
                   "git clone" \
                   "${REPO_DIR}"); do
    log.info "Updating git clone URLs in ${mdf}"
     sed -r -i "s|^(.*git clone) .*google-cloud-daos.*|\1 ${REMOTE_URL}.git|g" "${mdf}"
    grep -o -E 'git clone.*' "${mdf}" --color=auto
    echo
  done
  echo
}

main() {
  opts "$@"
  log.info "Updating 'Open in Google Cloud Shell' links in *.md files

    git_repo=${REMOTE_URL}
    cloudshell_git_branch=${BRANCH}
  "
  cd "${SCRIPT_DIR}/../../"
  REPO_DIR=$(pwd)
  update_cloud_shell_urls
  update_git_urls
}

main "$@"
