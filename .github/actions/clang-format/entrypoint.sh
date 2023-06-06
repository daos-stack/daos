#!/bin/sh

git config --global --add safe.directory /github/workspace

# Fix any formatting.
git-clang-format "${INPUT_TARGET}"

git diff

# Now revert any changes to auto-generated files.
find . -name "*.pb-c.c" -exec git checkout --quiet ./'{}' \;
find . -name "*.pb-c.h" -exec git checkout --quiet ./'{}' \;
find . -name "*.proto" -exec git checkout --quiet ./'{}' \;

git diff > auto-format-changes.diff

cat auto-format-changes.diff

if [ -s auto-format-changes.diff ]
then
        echo "Changes advised"
        exit 1
fi
exit 0
