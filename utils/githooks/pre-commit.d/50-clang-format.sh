#!/usr/bin/env bash
#
# Copyright 2022-2024 Intel Corporation.
# Copyright 2025 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -ue

_print_githook_header "Clang-format"
if [ -e .git/MERGE_HEAD ]; then
    echo "Merge commit. Skipping"
    exit 0
fi

if ! command -v git-clang-format > /dev/null 2>&1; then
    echo "git-clang-format not installed."
    echo "See ./utils/githooks/README.md#2-install-the-required-tools for instructions."
    exit 1
fi

# Check version of clang-format, and print a helpful message if it's too old.  If the right version
# is not found then exit.
./site_scons/site_tools/extra/extra.py > /dev/null || exit 0

echo "Formatting C files"

git-clang-format --staged src
