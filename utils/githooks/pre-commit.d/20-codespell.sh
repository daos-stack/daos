#!/bin/bash
#
#  Copyright 2024 Intel Corporation.
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
_git_diff_cached_files | xargs codespell
