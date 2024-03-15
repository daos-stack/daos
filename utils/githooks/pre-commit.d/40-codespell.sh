#!/bin/bash
#
#  Copyright 2024 Intel Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
#  Runs codespell for the DAOS project as a commit hook.
#

set -ue

echo "CodeSpell:"
# shellcheck disable=SC1091

if ! command -v codespell > /dev/null 2>&1
then
    echo "  codespell not installed. Install codespell command to improve pre-commit checks"
    echo "  python3 -m pip install -r ./utils/cq/requirements.txt"
    exit 0
fi

if [ "$TARGET" = "HEAD" ]; then
        echo "  Checking against HEAD"
        git diff HEAD --name-only | xargs codespell
else
        echo "  Checking against branch ${TARGET}"
        git diff "$TARGET"... --name-only | xargs codespell
fi
