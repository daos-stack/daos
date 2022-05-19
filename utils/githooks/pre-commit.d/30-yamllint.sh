#!/bin/bash

# Runs yamllint for the DAOS project.
#
# Picks up yamllint config settings from .yamllint

set -ue

if ! command -v yamllint > /dev/null 2>&1; then
    echo "No yaml checking, install yamllint command to improve pre-commit checks"
    echo "python3 -m pip install yamllint"
    exit 0
fi

echo "Checking yaml formatting"
IFS=' ' read -r -a yaml_files <<< "$(git diff --cached --name-only --diff-filter=ACMRTUXB | grep -E '\.ya?ml$')"
if [ ${#yaml_files[@]} -gt 0 ]; then
    yamllint --strict "${yaml_files[@]}"
fi
