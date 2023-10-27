#!/bin/sh

# Runs isort for the DAOS project.

set -ue

# Disable isort check because it doesn't work
# - it needs to run with -rc
# - it conflicts with pylint checks
# - ~100 files are flagged with import errors
# - it's really slow
exit 0

if ! command -v isort > /dev/null 2>&1
then
    echo "No isort checking, install isort command to improve pre-commit checks"
    echo "python3 -m pip install isort"
    exit 0
fi

echo "isort:"

echo "  Running isort for python imports."
isort --jobs 8 --check-only .
