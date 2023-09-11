#!/bin/sh

# Runs flake8 for the DAOS project.
#
# Will first check uncommitted code, then either the entire tree or against an entire
# pull request if possible.
#
# To get the most out of this hook the 'gh' command should be installed and working.
#
# Picks up flake config settings from .flake8

# Flake8 has removed the --diff option, to make this check work you need a previous version
# of flake.
# python3 -m pip install "flake8<6.0.0"

set -ue

echo "Flake8:"
if ! command -v flake8 > /dev/null 2>&1; then
    echo "  No flake checking, install flake8 command to improve pre-commit checks"
    exit 0
fi

if [ ! -f .flake8 ]; then
    echo "  No .flake8, skipping flake checks"
    exit 0
fi

echo "  Checking uncommitted code with flake."
git diff -u | flake8 --diff

if ! BRANCH=origin/$(git rev-parse --abbrev-ref HEAD 2>/dev/null); then
    echo "  Failed to determine branch with git rev-parse"
    exit 1
fi

if [ "$BRANCH" = "origin/master" ]; then
    echo "  Checking tree"
    flake8 --statistics
else

    # shellcheck disable=SC1091
    . utils/githooks/find_base.sh

    if [ "$TARGET" = "HEAD" ]; then
        echo "  Checking against branch HEAD"
        git diff HEAD -U10 | flake8 --config .flake8 --diff

        echo "  Checking scons code against branch ${TARGET}"
        git diff HEAD -U10 | flake8 --config .flake8-scons --diff
    else

        echo "  Checking against branch ${TARGET}"
        git diff "$TARGET"... -U10 | flake8 --config .flake8 --diff

        echo "  Checking scons code against branch ${TARGET}"
        git diff "$TARGET"... -U10 | flake8 --config .flake8-scons --diff
    fi
fi
