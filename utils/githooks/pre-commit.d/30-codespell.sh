#!/bin/sh

set -ue

if ! command -v codespell > /dev/null 2>&1
then
    echo "No spell checking, install codespell command to improve pre-commit checks"
    echo "python3 -m pip install codespell"
    exit 0
fi

if ! BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null); then
    echo "Failed to determine branch with git rev-parse"
    exit 1
fi

if [ "$BRANCH" = "master" ]
then
        TARGET=HEAD
else

    # Try and use the gh command to work out the target branch, or if not installed
    # then assume master.
    if command -v gh > /dev/null 2>&1
    then
	# If there is no PR created yet then do not check anything.
	if ! TARGET=$(gh pr view "$BRANCH" --json baseRefName -t "{{.baseRefName}}")
	then
	    exit 0
	fi
    else
	# With no 'gh' command installed then check against master.
	echo "Install gh command to auto-detect target branch, assuming master."
	TARGET=master
    fi
fi

echo "Checking spelling against branch ${TARGET}"

# In a per-commit hook.
mfiles=$(git diff-index --cached --name-only "$TARGET")
for file in $mfiles
do
        # Handle missing/removed files or files which exist on target but not the branch.
        if [ -f "$file" ]
        then
                # git reset $file
                codespell -w --ignore-words ci/codespell.ignores --builtin clear,rare,informal,names,en-GB_to_en-US --skip *.png,*.PNG,*.pyc,src/rdb/raft/*,src/control/vendor/*,RSA.golden,utils/rpms/* $file
                git add $file
        fi
done
