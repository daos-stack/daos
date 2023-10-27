#!/bin/sh

# Runs isort for the DAOS project.

set -ue


if ! command -v isort > /dev/null 2>&1
then
    echo "No isort checking, install isort command to improve pre-commit checks"
    echo "python3 -m pip install -r ./utils/cq/requirements.txt"
    exit 0
fi

echo "isort:"

echo "  Running isort for python imports."
isort --jobs 8 --check-only .
