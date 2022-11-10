#!/bin/sh

# Runs pylint for the DAOS project as a commit hook.
#
# To get the most out of this hook the 'gh' command should be installed and working.

set -ue

if ! BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null); then
        echo "Failed to determine branch with git rev-parse"
        exit 1
fi

# Try and use the gh command to work out the target branch, or if not installed
# then assume master.
if command -v gh > /dev/null 2>&1
then
        # If there is no PR created yet then do not check anything.
        if ! TARGET=origin/$(gh pr view "$BRANCH" --json baseRefName -t "{{.baseRefName}}")
        then
                TARGET=HEAD
        fi
else
        # With no 'gh' command installed then check against master.
        echo "Install gh command to auto-detect target branch, assuming master."
        TARGET=origin/master
fi

if [ $TARGET = "HEAD" ]
then
        echo "Checking against HEAD"
        git diff HEAD^ -U10 | ./utils/cq/daos_pylint.py --diff
else
        echo "Checking against branch ${TARGET}"
        git diff $TARGET... -U10 | ./utils/cq/daos_pylint.py --diff
fi
