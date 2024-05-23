#!/bin/sh
#
# Copyright 2024 Intel Corporation.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Runs "tags.py lint" for ftest changes.
#

set -ue

_print_githook_header "Ftest"

if ! python3 -c 'import yaml' > /dev/null 2>&1; then
    echo "python3 requirements not installed. Install requirements to improve pre-commit checks:"
    echo "  python3 -m pip install -r ./utils/cq/requirements.txt"
    exit 0
fi

echo "Linting modified files"

_git_diff_cached_files '*/ftest/*.py' | xargs -r python3 src/tests/ftest/tags.py lint
