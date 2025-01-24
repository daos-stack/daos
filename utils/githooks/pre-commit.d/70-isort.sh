#!/usr/bin/env bash
#
# Copyright 2023-2024 Intel Corporation.
# Copyright 2025 Hewlett Packard Enterprise Development LP
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
fi

echo "Checking if python imports are sorted"

