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
if ! git fetch origin "${TARGET_BRANCH}"; then
    echo "Hrm.  Got an error fetching the target branch"
    # nothing to do here except exit as if it's not a doc-only change
    exit 0
fi
if ! merge_base="$(git merge-base "FETCH_HEAD" HEAD)"; then
    echo "Hrm.  Got an error finding the merge base"
    # nothing to do here except exit as if it's not a doc-only change
    exit 0
fi
if ! git diff --no-commit-id --name-only "$merge_base" HEAD | grep -q -e ".*"; then
    echo "No changes detected, full validation is expected"
    exit 0
fi
git diff --no-commit-id --name-only "$merge_base" HEAD | \
  grep -v -e "^docs/" -e "\.md$" -e "^.*LICENSE.*$"
