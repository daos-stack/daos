#!/bin/bash

if ! BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null); then
    echo "  Failed to determine branch with git rev-parse"
    exit 1
fi

ORIGIN="origin"
if [ -n "$DAOS_ORIGIN" ]; then
    ORIGIN="$DAOS_ORIGIN"
else
    echo "Using origin as remote repo.  If this is incorrect, set DAOS_ORIGIN in environment"
fi

# Try and use the gh command to work out the target branch, or if not installed
# then assume origin/master.
if command -v gh > /dev/null 2>&1; then
    # If there is no PR created yet then do not check anything.
    if ! TARGET="$ORIGIN"/$(gh pr view "$BRANCH" --json baseRefName -t "{{.baseRefName}}"); then
        TARGET=HEAD
    fi
else
    # With no 'gh' command installed then check against origin/master.
    echo "  Install gh command to auto-detect target branch, assuming origin/master."
    # shellcheck disable=SC2034
    TARGET="$ORIGIN"/master
fi

# get the actual commit in $TARGET that is our base, if we are working on a commit in the history
# of $TARGET and not it's HEAD
TARGET=$(git merge-base HEAD "$TARGET")
