#!/bin/bash
#
# Copyright 2023-2024 Intel Corporation.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Runs isort for the DAOS project.

set -ue

_print_githook_header "isort"

if ! command -v isort > /dev/null 2>&1; then
    echo "isort not installed. Install isort command to improve pre-commit checks:"
    echo "  python3 -m pip install -r ./utils/cq/requirements.txt"
    . /etc/os-release
    if [ "$ID" = "fedora" ]; then
        echo "  or"
        echo "  dnf install python3-isort"
    fi
    exit 0
fi

echo "Checking if python imports are sorted"

isort_args=(--jobs 8 .)
if ! isort --check-only "${isort_args[@]}"; then
    echo "  isort check failed, run 'isort ${isort_args[*]}' to fix."
    exit 1
fi
