#!/bin/sh

# Prepare the source tree for Doxygen, see .github/workflows/doxygen.yml

set -ex

# Load the list of modified files.
git fetch
git branch -lr
FILES=$(git diff --name-only origin/master... src/include)

# Create a new temp directory.
mkdir src/include2

# Copy modified files into it.
for FILE in $FILES
do
    cp "$FILE" ./src/include2
done

# Move real path aside.
mv src/include src/include.old

# And place the modified ones back in place.
mv src/include2 src/include

# Ensure there is at least one file present.
if [ -f src/include.old/daos.h ]
then
    mv src/include.old/daos.h src/include
fi
