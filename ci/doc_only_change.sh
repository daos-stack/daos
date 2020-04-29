#!/bin/bash

CHANGE_ID=%s
target_branch=%s

if [ "$CHANGE_ID" = "null" ]; then
     mb_modifier=^
fi
git diff-tree --no-commit-id --name-only              \
  "$(git merge-base origin/$target_branch$mb_modifier HEAD)" HEAD | \
  grep -v -e "^doc$"
