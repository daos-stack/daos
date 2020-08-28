#!/bin/bash

# This script is used to help a CI system determine if a git commit
# made only a change to documentation.

set -uex

mb_modifier=
if [ "$CHANGE_ID" = "null" ]; then
    mb_modifier=^
fi

# The fetch of $TARGET_BRANCH gets the branch for the compare as a commit hash
# with the temporary name FETCH_HEAD.
git fetch origin "${TARGET_BRANCH}"
git diff-tree --no-commit-id --name-only                                \
  "$(git merge-base "FETCH_HEAD$mb_modifier" HEAD)" HEAD | \
  grep -v -e "^doc$"
