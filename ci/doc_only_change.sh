#!/bin/bash

# This script is used to help a CI system determine if a git commit
# made only a change to documentation.

set -uex

# However note, that we don't want to do this on branch landings since
# we want the full run for a branch landing so that the lastSuccessfulBuild
# will always have artifacts in it.
if [ "$CHANGE_ID" = "null" ]; then
    # exiting 1 means doc-only change
    exit 0
fi

# The fetch of $TARGET_BRANCH gets the branch for the compare as a commit hash
# with the temporary name FETCH_HEAD.
git fetch origin "${TARGET_BRANCH}"
git diff-tree --no-commit-id --name-only                                \
  "$(git merge-base "FETCH_HEAD" HEAD)" HEAD | \
  grep -v -e "^doc$"