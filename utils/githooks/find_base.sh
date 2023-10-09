#!/bin/bash
# /*
#  * (C) Copyright 2023 Intel Corporation.
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
# */

echo "Checking for target branch"

if ! BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null); then
    echo "  Failed to determine branch with git rev-parse"
    exit 1
fi

ORIGIN="${DAOS_ORIGIN:=origin}"
if [ "$ORIGIN" == "origin" ]; then
    echo "  Using origin as remote repo.  If this is incorrect, set DAOS_ORIGIN in environment"
fi

# Try and use the gh command to work out the target branch, or if not installed
# then assume origin/master.
if command -v gh > /dev/null 2>&1; then
    # If there is no PR created yet then do not check anything.
    if ! TARGET="$ORIGIN"/$(gh pr view "$BRANCH" --json baseRefName -t "{{.baseRefName}}"); then
        TARGET=HEAD
    else
        state=$(gh pr view "$BRANCH" --json state -t "{{.state}}")
        if [ ! "$state" = "OPEN" ]; then
            TARGET=HEAD
        fi
    fi
else
    # With no 'gh' command installed, use the "closest" branch as the target,
    # calculated as the sum of the commits this branch is ahead and behind.
    # check master, then current release branches, then current feature branches.
    # shellcheck disable=SC2034
    all_bases=("master")
    all_bases+=($(git branch --list -r "$ORIGIN/release/*" \
                | grep -oE "release/2\.[4-9]+.*|release/[3-9]+.*"))
    all_bases+=("feature/cat_recovery" "feature/multiprovider")
    TARGET="$ORIGIN/master"
    min_diff=-1
    for base in "${all_bases[@]}"; do
        git rev-parse --verify "$base" 2&>/dev/null || continue
        commits_ahead=$(git log --oneline "$ORIGIN/$base..HEAD" | wc -l)
        commits_behind=$(git log --oneline "HEAD..$ORIGIN/$base" | wc -l)
        commits_diff=$((commits_ahead + commits_behind))
        if [ "$min_diff" -eq -1 ] || [ "$min_diff" -gt "$commits_diff" ]; then
            TARGET="$ORIGIN/$base"
            min_diff=$commits_diff
        fi
    done
    echo "  Install gh command to auto-detect target branch, assuming $TARGET."
fi

# get the actual commit in $TARGET that is our base, if we are working on a commit in the history
# of $TARGET and not it's HEAD
TARGET=$(git merge-base HEAD "$TARGET")
