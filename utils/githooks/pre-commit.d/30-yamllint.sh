#!/bin/bash

# Runs yamllint for the DAOS project.
#
# Picks up yamllint config settings from .yamllint.yaml
# The yamllint tool will use .yamllint in preference to this file and it is in .gitignore so users
# can create one locally to overrule these settings for local commit hooks.
set -ue

if ! command -v yamllint > /dev/null 2>&1
then
    echo "No yaml checking, install yamllint command to improve pre-commit checks"
    echo "python3 -m pip install yamllint"
    exit 0
fi

echo "Checking yaml formatting"
targets=(
    '*.yml'
    '*.yaml'
)

git diff --diff-filter=ACMRTUXB --name-only --cached -z -- "${targets[@]}" |\
    xargs -r0 yamllint --strict
