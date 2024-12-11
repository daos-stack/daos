#!/bin/bash
#
# Copyright 2022-2024 Intel Corporation.
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
    echo "git-clang-format not installed. Skipping"
    exit 0
fi
if ! command -v clang-format > /dev/null 2>&1; then
    echo "clang-format not installed. Skipping"
    exit 0
fi

echo "Formatting C files"

# Check version of clang-format, and print a helpful message if it's too old.  If the right version
# is not found then exit.
./site_scons/site_tools/extra/extra.py || exit 0

git-clang-format --staged src
