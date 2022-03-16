#!/bin/sh

# Runs flake8 for the DAOS project.
# Will first check uncomiited code, then either the entire tree if on master
# or a diff against master if not.
#
# Picks up config files from .flake8

if ! command -v flake8 > /dev/null 2>&1
then
    echo "No flake checking, install flake8 command"
    exit 0
fi

echo checking uncommitted code.
git diff -u | flake8 --diff

RC=$?
if [ $RC -ne 0 ]
then
    exit $RC
fi

BRANCH=$(git rev-parse --abbrev-ref HEAD > /dev/null 2>&1)

if [ "$BRANCH" = "master" ]
then
    echo Checking tree
    flake8 --statistics
else
    echo Checking against master.

    # Try and use the gh command to work out the target branch, or if not installed
    # then assume master.
    if command -v gh > /dev/null 2>&1
    then
	TARGET=$(gh pr view "$BRANCH" --json baseRefName -t "{{.baseRefName}}")
	# If there is no PR yet then use master.
	if [ $? -ne 0 ]
	then
	    exit 0
	fi
    else
	TARGET=master
    fi

    git diff $TARGET... -u | flake8 --diff
fi
