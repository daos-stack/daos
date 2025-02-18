#!/usr/bin/env bash
#
#  Copyright 2024 Intel Corporation.
#  Copyright 2025 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
#  Runs codespell for the DAOS project as a commit hook.
#

set -ue

_print_githook_header "CodeSpell"
# shellcheck disable=SC1091

if ! command -v codespell > /dev/null 2>&1
then
    echo "codespell not installed. Install codespell command to improve pre-commit checks:"
    echo "  python3 -m pip install -r ./utils/cq/requirements.txt"
    exit 0
fi

echo "Checking for spelling mistakes"
# Convert file names to relative path format that codespell expects. I.e. "./path"
_git_diff_cached_files | xargs -r -n 1 -I% echo "./%" | xargs -r codespell
