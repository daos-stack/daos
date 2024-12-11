#!/bin/bash
#
#  Copyright 2024 Intel Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Check or update copyright date in modified files.
# Usage: check_update_copyright.sh <git_target> <githook|gha>
#   mode "githook" will update copyright dates in place.
#   mode "gha" will just print a warning in a GHA-compatible format.

set -e

git_target="$1"
mode="$2"
case "$mode" in
    "githook" | "gha")
        ;;
    *)
        echo "Usage: check_update_copyright.sh <git_target> <githook|gha>"
        exit 1
esac

# Navigate to repo root
PARENT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$PARENT_DIR"/../../


regex='(^[[:blank:]]*[\*/]*.*)((Copyright[[:blank:]]*)([0-9]{4})(-([0-9]{4}))?)([[:blank:]]*(Intel.*$))'
year=$(date +%Y)
errors=0
targets=(
    # Entries with wildcard.  These must be first and start with '*' or
    # older versions of git will return files that were not changed.
    '*.c'
    '*.h'
    '*.go'
    '*.py'
    '*.proto'
    '*.java'
    '*.yml'
    '*.yaml'
    '*.sh'
    '*.bash'
    '*Dockerfile*'
    '*README*'
    '*LICENSE*'
    '*NOTICE*'
    '*.txt'
    '*.md'
    # Entries without a wildcard
    'Makefile'
    'Jenkinsfile'
    'SConscript'
    'SConstruct'
    'copyright'
    '.env'
)

if [ -z "$files" ]; then
    files=$(git diff "$git_target" --cached --diff-filter=AM --name-only -- "${targets[@]}")
else
    echo "  Checking against custom files"
fi

os=$(uname -s)

. utils/githooks/git-version.sh

for file in $files; do
    if [[ "$file" == *vendor* ]] || [[ "$file" == *pb.go ]]    ||
       [[ "$file" == *_string.go ]] || [[ "$file" == *pb-c* ]] ||
       { [ "$mode" == "githook" ] &&
         [ "$git_vercode" -ge 2030000 ] &&
         [ "$(git diff --cached -I Copyright "$file")" = '' ]; }; then
        continue
    fi
    read -r y1 y2 <<< "$(sed -nre "s/^.*$regex.*$/\4 \6/p" "$file")"
    if [[ -z $y1 ]] ; then
        # Print warning but don't error on non-existent copyright 
        echo "  Copyright Information not found in: $file"
    elif [[ $y1 -ne $year && $year -ne $y2 ]] ; then
        if [[ "$mode" == "githook" ]]; then
            # Update copyright in place
            if ! git reset "$file"; then
                echo "  Unable to un-stage $file"
                errors=$((errors + 1))
            fi
            if [[ "$os" == 'Linux' ]]; then
                sed -i -re "s/$regex/\1Copyright $y1-$year \8/" "$file"
            else
                sed -i '' -re "s/$regex/\1Copyright $y1-$year \8/" "$file"
            fi

            if ! git add "$file"; then
                echo "  Unable to re-stage $file"
                errors=$((errors + 1))
            fi
        elif [[ "$mode" == "gha" ]]; then
            # Print error but do not update
            lineno="$(grep -nE "$regex" "$file" | cut -f1 -d:)"
            echo "::error file=$file,line=$lineno::Copyright out of date"
            errors=$((errors + 1))
        fi
    fi
done

if [[ $errors -ne 0 ]]; then
    echo "  $errors errors while checking/fixing copyrights."
    exit 1
fi
