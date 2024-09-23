#!/bin/bash
# /*
#  * (C) Copyright 2024 Intel Corporation.
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
# */

echo "Checking for target branch"

if ! BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null); then
    echo "  Failed to determine branch with git rev-parse"
    exit 1
fi

ORIGIN="${DAOS_ORIGIN:=origin}"
if [ "$ORIGIN" = "origin" ]; then
    echo "  Using origin as remote repo.  If this is incorrect, set DAOS_ORIGIN in environment"
fi

# Try and use the gh command to work out the target branch, or if not installed
# then assume origin/master.
TARGET=""
if ${USE_GH:-true} && command -v gh > /dev/null 2>&1; then
    # If there is no PR created yet then do not check anything.
    if ! TARGET="$ORIGIN"/$(gh pr view "$BRANCH" --json baseRefName -t "{{.baseRefName}}"); then
        TARGET=""
    else
        state=$(gh pr view "$BRANCH" --json state -t "{{.state}}")
        if [ ! "$state" = "OPEN" ]; then
            TARGET=""
        fi
    fi
fi

if [ -z "$TARGET" ]; then
    # With no 'gh' command installed, or no PR open yet, use the "closest" branch
    # as the target, calculated as the sum of the commits this branch is ahead and
    # behind.
    # check master, then current release branches, then current feature branches.
    TARGET="$ORIGIN/$(utils/rpms/packaging/get_release_branch "feature/cat_recovery feature/multiprovider")"
    echo "  Install gh command to auto-detect target branch, assuming $TARGET."
fi

# get the actual commit in $TARGET that is our base, if we are working on a commit in the history
# of $TARGET and not it's HEAD
if [ -e .git/MERGE_HEAD ]; then
    # Use common ancestor between the target, this HEAD, and the being-merged MERGE_HEAD
    TARGET=$(git merge-base "$TARGET" HEAD MERGE_HEAD)
else
    # Use common ancestor between the target and this HEAD
    TARGET=$(git merge-base "$TARGET" HEAD)
fi
