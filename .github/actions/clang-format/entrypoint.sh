#!/bin/sh

git config --global --add safe.directory /github/workspace

# Use merge base of the target instead of the tip of the target
if [ -n "${INPUT_TARGET}" ]; then
    INPUT_TARGET="$(git merge-base "${INPUT_TARGET}" HEAD)"
fi

# Fix any formatting.
echo "Applying code formatting"
git-clang-format "${INPUT_TARGET}"

echo "Reverting any suggestions made to generated files"
# Now revert any changes to auto-generated files.
find . -name "*.pb-c.c" -exec git checkout --quiet ./'{}' \;
find . -name "*.pb-c.h" -exec git checkout --quiet ./'{}' \;
find . -name "*.proto" -exec git checkout --quiet ./'{}' \;
find . -name "*.json" -exec git checkout --quiet ./'{}' \;

echo "Showing recommended formatting changes"
git diff > auto-format-changes.diff

cat auto-format-changes.diff

if [ -s auto-format-changes.diff ]
then
        echo "Changes advised"
        exit 1
fi
exit 0
