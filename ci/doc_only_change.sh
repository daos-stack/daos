#!/bin/bash

# This script is used to help a CI system determine if a git commit
# made only a change to documentation.

set -uex

if [ "$CHANGE_ID" = "null" ]; then
    mb_modifier=^
fi
git diff-tree --no-commit-id --name-only                                \
  "$(git merge-base origin/"${TARGET_BRANCH}"$mb_modifier HEAD)" HEAD | \
  grep -v -e "^doc$"
