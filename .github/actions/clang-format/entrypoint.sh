#!/bin/sh

set -e

git config --global --add safe.directory /github/workspace

set +e

# Show what would be changed as a diff.
git-clang-format "${INPUT_TARGET}" --diffstat --verbose
git-clang-format "${INPUT_TARGET}" --diff --quiet > auto-format-changes.diff
cat auto-format-changes.diff

if [ -s auto-format-changes.diff ]
then
        echo "Changes advised"
        exit 1
fi
exit 0
