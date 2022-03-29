#!/bin/sh

# Runs yamllint for the DAOS project.
#
# Will first check uncomiited code, then either the entire tree or against an entire
# pull request if possible.
#
# To get the most out of this hook the 'gh' command should be installed and working.
#
# Picks up yamllint config settings from .yamllint

set -ue

if ! command -v yamllint > /dev/null 2>&1
then
    echo "No yaml checking, install yamllint command to improve pre-commit checks"
    exit 0
fi

echo "Checking yaml formatting"
yamllint --strict .
