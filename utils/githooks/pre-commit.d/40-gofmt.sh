#!/bin/bash

# Runs gofmt for the DAOS project as a commit hook.
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

go_files=
if [ $TARGET = "HEAD" ]
then
        echo "Checking against HEAD"
	go_files=$(git diff HEAD --name-only | grep -e '.go$' || exit 0)
else
        echo "Checking against branch ${TARGET}"
        go_files=$(git diff $TARGET... --name-only | grep -e '.go$' || exit 0)
fi

if [ -z "$go_files" ]; then
	exit 0
fi

if ! which gofmt >/dev/null 2>&1; then
	echo "ERROR: Changed .go files found but gofmt is not in your PATH"
	exit 1
fi

output=$(echo "$go_files" | xargs gofmt -d)
if [ -n "$output" ]; then
        echo "ERROR: Your code hasn't been run through gofmt!"
        echo "Please configure your editor to run gofmt on save."
        echo "Alternatively, at a minimum, run the following command:"
        echo -n "find src/control -name '*.go' -and -not -path '*vendor*'"
        echo "| xargs gofmt -w"
        echo -e "\ngofmt check found the following:\n\n$output\n"
        exit 1
fi
