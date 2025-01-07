#!/bin/bash
#
# Copyright 2022-2024 Intel Corporation.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Runs flake8 for the DAOS project.
#
# Will first check uncommitted code, then either the entire tree or against an entire
# pull request if possible.
#
# To get the most out of this hook the 'gh' command should be installed and working.
#
# Picks up flake config settings from .flake8

# Flake8 has removed the --diff option, to make this check work you need a previous version
# of flake.
# python3 -m pip install "flake8<6.0.0"

set -ue

_print_githook_header "Flake8"

py_files=$(_git_diff_cached_files "*.py SConstruct */SConscript")

if [ -z "$py_files" ]; then
    echo "No python changes. Skipping"
    exit 0
fi

if ! command -v flake8 > /dev/null 2>&1; then
    echo "flake8 not installed. Install flake8 command to improve pre-commit checks:"
    echo "  python3 -m pip install -r ./utils/cq/requirements.txt"
    exit 0
fi

echo "Linting python"

if ! BRANCH=origin/$(git rev-parse --abbrev-ref HEAD 2>/dev/null); then
    echo "  Failed to determine branch with git rev-parse"
    exit 1
fi

if [ "$BRANCH" = "origin/master" ]; then
    echo "  Checking tree"
    flake8 --statistics
else
    rc=0

    # non-scons
    if ! echo "$py_files" | grep -vi scons | xargs -r flake8 --config .flake8; then
        rc=1
    fi

    # scons
    if ! echo "$py_files" | grep -i scons | xargs -r flake8 --config .flake8-scons; then
        rc=1;
    fi

    exit "$rc"
fi
