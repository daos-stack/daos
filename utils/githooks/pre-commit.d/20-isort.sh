#!/bin/bash

# Runs isort for the DAOS project.

set -ue

if ! command -v isort > /dev/null 2>&1; then
    echo "No isort checking, install isort command to improve pre-commit checks"
    . /etc/os-release
    if [ "$ID" = "fedora" ]; then
        echo "dnf install python3-isort"
        echo "or"
    fi
    echo "python3 -m pip install -r ./utils/cq/requirements.txt"
    exit 0
fi

echo "isort:"

echo "  Running isort for python imports."
isort_args=(--jobs 8 .)
if ! isort --check-only "${isort_args[@]}"; then
    echo "  isort check failed, run 'isort ${isort_args[*]}' to fix."
    exit 1
fi
