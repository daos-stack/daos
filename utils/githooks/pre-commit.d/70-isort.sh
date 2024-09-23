#!/bin/bash
#
# Copyright 2023-2024 Intel Corporation.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Runs isort for the DAOS project.

set -ue

_print_githook_header "isort"

py_files=$(_git_diff_cached_files "*.py SConstruct */SConscript")

if [ -z "$py_files" ]; then
    echo "No python changes. Skipping"
    exit 0
fi

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

if ! echo "$py_files" | xargs -r isort --check-only --jobs 8; then
    echo "  isort check failed, run 'isort --jobs 8 .' to fix."
    exit 1
fi
