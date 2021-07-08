#!/bin/bash

# Script to run doxygen against modified headers and report results.
# see .github/workflows/doxygen.yml

set -e

echo ::group::Setting up src

# Load the list of modified files.
git fetch
mapfile -t FILES < <(git diff --name-only origin/master... src/include/*.h)

# Ensure there is at least one file present in new dir.
if [ ${#FILES[@]} -eq 0 ];
then
    echo No headers modified, exiting.
    echo ::endgroup::
    exit 0
fi

# Make a new directory.
mkdir src/include2

# Copy the files to new dir.
cp "${FILES[@]}" ./src/include2

# Move the real files aside
mv src/include src/include.old

# Move in the ones to test.
mv src/include2 src/include

echo ::endgroup::

echo ::group::Installing doxygen
sudo apt-get install doxygen
echo ::endgroup::

echo ::group::Running check
echo ::add-matcher::ci/daos-doxygen-matcher.json
doxygen Doxyfile
echo ::remove-matcher owner=daos-doxygen::
echo ::endgroup::
