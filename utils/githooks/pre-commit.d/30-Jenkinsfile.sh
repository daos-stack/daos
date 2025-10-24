#!/usr/bin/env bash
# shellcheck disable=SC2317
#
#  Copyright 2023-2024 Intel Corporation.
#  Copyright 2025 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Checks the Jenkinsfile for syntax errors
#
# Will only check if Jenkinsfile is modified
#
# shellcheck disable=SC2317

set -ue

_print_githook_header "Jenkinsfile"

if [ -z "$(_git_diff_cached_files "Jenkinsfile")" ] ; then
    echo "No Jenkinsfile changes. Skipping"
    exit 0
fi


echo "Checking syntax"
: "${JENKINS_HOST:=jenkins.daos.hpc.amslabs.hpecorp.net}"

# shellcheck disable=SC2317
if ! ping -c 1 "$JENKINS_HOST" &> /dev/null; then
    echo "Failed to access $JENKINS_HOST. Skipping"
    exit 0
fi

: "${CURL_VERBOSE:=}"
CURL_OPTS=("$CURL_VERBOSE")
echo "Checking Jenkinsfile syntax on ${JENKINS_HOST}"
URL="https://$JENKINS_HOST/pipeline-model-converter/validate"
if ! output=$(curl "${CURL_OPTS[@]}" -s -X POST -F "jenkinsfile=<${1:-Jenkinsfile}" "$URL"); then
    echo "  Failed to access $URL. Skipping"
    exit 0
fi

if echo "$output" | grep -q "Errors encountered validating"; then
    echo "$output"
    exit 1
fi
