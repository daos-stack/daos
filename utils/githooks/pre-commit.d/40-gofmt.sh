#!/bin/bash

# Runs gofmt for the DAOS project as a commit hook.
#
# To get the most out of this hook the 'gh' command should be installed and working.

set -ue

echo "Gofmt:"
# shellcheck disable=SC1091

go_files=
if [ "$TARGET" = "HEAD" ]; then
        echo "  Checking against HEAD"
	go_files=$(git diff HEAD --name-only | grep -e '.go$' || exit 0)
else
        echo "  Checking against branch ${TARGET}"
        go_files=$(git diff "$TARGET"... --name-only | grep -e '.go$' || exit 0)
fi

if [ -z "$go_files" ]; then
	exit 0
fi

if ! which gofmt >/dev/null 2>&1; then
	echo "  ERROR: Changed .go files found but gofmt is not in your PATH"
	exit 1
fi

output=$(echo "$go_files" | xargs gofmt -d)
if [ -n "$output" ]; then
        echo "  ERROR: Your code hasn't been run through gofmt!"
        echo "  Please configure your editor to run gofmt on save."
        echo "  Alternatively, at a minimum, run the following command:"
        echo -n "  find src/control -name '*.go' -and -not -path '*vendor*'"
        echo " | xargs gofmt -w"
        echo -e "\n  gofmt check found the following:\n\n$output\n"
        exit 1
fi
