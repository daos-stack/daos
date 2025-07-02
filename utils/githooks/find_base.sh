#!/usr/bin/env bash
# /*
#  * (C) Copyright 2024 Intel Corporation.
#  * (C) Copyright 2025 Google LLC
#  * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
# */

echo "Checking for target branch"

if ! BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null); then
    echo "  Failed to determine branch with git rev-parse"
    exit 1
fi

TARGET_BRANCH=""
# Try and use the gh command to work out the target branch, or if not installed
# then assume origin/master.
if ${USE_GH:-true} && command -v gh > /dev/null 2>&1; then
    # If there is no PR created yet then do not check anything.
    if ! TARGET_BRANCH="$(gh pr view "$BRANCH" --json baseRefName -t "{{.baseRefName}}")"; then
        TARGET_BRANCH=""
    else
        state=$(gh pr view "$BRANCH" --json state -t "{{.state}}")
        if [ ! "$state" = "OPEN" ]; then
          TARGET_BRANCH=""
        fi
    fi
fi

if [ -z "$TARGET_BRANCH" ]; then
    # With no 'gh' command installed, or no PR open yet, use the "closest" branch
    # as the target, calculated as the sum of the commits this branch is ahead and
    # behind. This will check any branches configured by a branches.* script
    TARGET="$(utils/githooks/get_branch)"
else
    # We don't know the remote for sure so let's run the checks anyway which
    # should come to the same answer but uses gh to get a better one
    TARGET="$(utils/githooks/get_branch "${TARGET_BRANCH}")"
fi

echo "Using ${TARGET} as base branch"

# get the actual commit in $TARGET that is our base, if we are working on a commit in the history
# of $TARGET and not it's HEAD
if [ -e .git/MERGE_HEAD ]; then
    # Use common ancestor between the target, this HEAD, and the being-merged MERGE_HEAD
    TARGET=$(git merge-base "$TARGET" HEAD MERGE_HEAD)
else
    # Use common ancestor between the target and this HEAD
    TARGET=$(git merge-base "$TARGET" HEAD)
fi
