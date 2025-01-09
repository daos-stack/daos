#!/usr/bin/env bash
#
# Copyright 2022-2024 Intel Corporation.
# Copyright 2025 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Runs pylint for the DAOS project as a commit hook.
#
# To get the most out of this hook the 'gh' command should be installed and working.

set -ue

_print_githook_header "Pylint"
# shellcheck disable=SC1091

echo "Linting python"

_git_diff_cached_files | ./utils/cq/daos_pylint.py --files-from-stdin
