#!/usr/bin/env bash
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

set -ue

_print_githook_header "Jenkinsfile"

if [ -z "$(_git_diff_cached_files "Jenkinsfile")" ] ; then
    echo "No Jenkinsfile changes. Skipping"
    exit 0
fi

echo "Checking syntax"

HOST="${HOST:-build.hpdd.intel.com}"
if ! ping -c 1 $HOST &> /dev/null; then
    echo "Failed to access $HOST. Skipping"
    exit 0
fi

CURL_VERBOSE=${CURL_VERBOSE:-""}
CURL_PROXY="${CURL_PROXY:+-x }${CURL_PROXY:-}"
CURL_OPTS=("$CURL_PROXY" "$CURL_VERBOSE" -s)

URL="https://$HOST/pipeline-model-converter/validate"
if ! output=$(curl "${CURL_OPTS[@]}" -s -X POST -F "jenkinsfile=<${1:-Jenkinsfile}" "$URL"); then
    echo "Failed to access $URL. Skipping"
    exit 0
fi

if echo "$output" | grep -q "Errors encountered validating"; then
    echo "$output"
    exit 1
fi
