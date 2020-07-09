#!/bin/bash

# This script is used to help a CI system determine if a git commit
# made only a change to documentation.

set -uex

mb_modifier=
if [ "$CHANGE_ID" = "null" ]; then
    mb_modifier=^
fi
# TARGET_BRANCH can not be used with stacked PRs, as it will
# emit an 'Not a valid object name' fatal error.
# In this case, fall back to the master branch.
git fetch origin/"${TARGET_BRANCH}"
git diff-tree --no-commit-id --name-only                                \
  "$(git merge-base origin/"${TARGET_BRANCH}"$mb_modifier HEAD)" HEAD | \
  grep -v -e "^doc$"
