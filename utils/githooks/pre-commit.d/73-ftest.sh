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

echo "Linting modified files"

_git_diff_cached_files '*/ftest/*.py' | xargs -r python3 src/tests/ftest/tags.py lint
