#!/usr/bin/env bash
#
# Copyright 2022-2024 Intel Corporation.
# Copyright 2025 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Runs yamllint for the DAOS project.
#
# Picks up yamllint config settings from .yamllint.yaml
# The yamllint tool will use .yamllint in preference to this file and it is in .gitignore so users
# can create one locally to overrule these settings for local commit hooks.
set -ue

_print_githook_header "Yaml Lint"

if ! command -v yamllint > /dev/null 2>&1
then
    echo "yamllint not installed. Install yamllint command to improve pre-commit checks:"
    echo "  python3 -m pip install -r ./utils/cq/requirements.txt"
    exit 0
fi

echo "Checking yaml formatting"

_git_diff_cached_files '*.yml *.yaml' '-z' | xargs -r0 yamllint --strict
    
