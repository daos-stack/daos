#!/bin/sh

# Runs flake8 for the DAOS project.
# Will first check uncomiited code, then either the entire tree if on master
# or a diff against master if not.
#
# Picks up config files from .flake8

echo checking uncommitted code.
git diff -u | flake8 --diff

RC=$?
if [ $RC -ne 0 ]
then
    exit $RC
fi

BRANCH=$(git rev-parse --abbrev-ref HEAD 2> /dev/null)

if [ "$BRANCH" == "master" ]
then
    echo Checking tree
    flake8 --statistics
else
    echo Checking against master.
    git diff master... -u | flake8 --diff
fi
