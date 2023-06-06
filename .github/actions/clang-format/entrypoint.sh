#!/bin/sh

set -e

git config --global --add safe.directory /github/workspace

set +e

# Show what would be changed as a diff.
git-clang-format "${INPUT_TARGET}" --diffstat

# Fix any formatting.
git-clang-format "${INPUT_TARGET}"

# Now revert any changes to auto-generated files.
find . -name *.pb-c.c -exec git checkout '{}' \;
find . -name *.pb-c.h -exec git checkout '{}' \;
find . -name *.proto -exec git checkout '{}' \;

git diff > auto-format-changes.diff

cat auto-format-changes.diff

if [ -s auto-format-changes.diff ]
then
        echo "Changes advised"
        exit 1
fi
exit 0
