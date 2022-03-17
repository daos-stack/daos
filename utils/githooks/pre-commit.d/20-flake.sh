#!/bin/sh

# Runs flake8 for the DAOS project.
# Will first check uncomiited code, then either the entire tree if on master
# or a diff against master if not.
#
# Picks up config files from .flake8

set -u

if ! command -v flake8 > /dev/null 2>&1
then
    echo "No flake checking, install flake8 command to improve pre-commit checks"
    exit 0
fi

echo Checking uncommitted code with flake.
git diff -u | flake8 --diff

RC=$?
if [ $RC -ne 0 ]
   exit $RC
fi

BRANCH=$(git rev-parse --abbrev-ref HEAD > /dev/null 2>&1)

if [ "$BRANCH" = "master" ]
then
    echo Checking tree
    flake8 --statistics
else

    # Try and use the gh command to work out the target branch, or if not installed
    # then assume master.
    if command -v gh > /dev/null 2>&1
    then
	# If there is no PR created yet then do not check anything.
	if ! TARGET=$(gh pr view "$BRANCH" --json baseRefName -t "{{.baseRefName}}")
	then
	    exit 0
	fi
    else
	# With no 'gh' command installed then check against master.
	echo "Install gh command to auto-detect target branch, assuming master."
	TARGET=master
    fi

    echo Checking against branch ${TARGET}
    git diff $TARGET... -u | flake8 --diff
fi
