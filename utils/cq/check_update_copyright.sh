#!/usr/bin/env bash
#
#  Copyright 2024 Intel Corporation.
#  Copyright 2025 Hewlett Packard Enterprise Development LP
#  Copyright 2025 Google LLC
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


regex_intel='(^[[:blank:]]*[\*/]*.*)((Copyright[[:blank:]]*)([0-9]{4})(-([0-9]{4}))?)([[:blank:]]*(Intel.*$))'
shortname_intel="Intel Corporation."
regex_hpe='(^[[:blank:]]*[\*/]*.*)((Copyright[[:blank:]]*)([0-9]{4})(-([0-9]{4}))?)([[:blank:]]*(Hewlett Packard Enterprise Development LP.*$))'
shortname_hpe="Hewlett Packard Enterprise Development LP"
regex_google='(^[[:blank:]]*[\*/]*.*)((Copyright[[:blank:]]*)([0-9]{4})(-([0-9]{4}))?)([[:blank:]]*(Google LLC.*$))'
shortname_google="Google LLC"
regex_enakta='(^[[:blank:]]*[\*/]*.*)((Copyright[[:blank:]]*)([0-9]{4})(-([0-9]{4}))?)([[:blank:]]*(Enakta.*$))'
shortname_enakta="Enakta Labs Ltd"
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

function git_reset() {
    local file="$1"
    if ! git reset "$file"; then
        echo "  Unable to un-stage $file"
        errors=$((errors + 1))
        return 1
    fi
    return 0
}

function git_add() {
    local file="$1"
    if ! git add "$file"; then
        echo "  Unable to re-stage $file"
        errors=$((errors + 1))
        return 1
    fi
    return 0
}


# Use HPE copyright for all users
# See below example to toggle copyright regex based on user
regex_user="$regex_hpe"
shortname_user="$shortname_hpe"
if [[ "$mode" == "githook" ]]; then
    # Extract domain from configured email
    user_domain="$(git config user.email | sed -n 's/^.*@\([-0-9a-zA-Z]*\).*/\1/p')"
else
    # Extract domain from the first Signed-off-by
    user_domain="$(git log -1 | grep 'Signed-off-by' | head -n 1 | sed -n 's/^.*@\([-0-9a-zA-Z]*\).*/\1/p')"
fi
case "$user_domain" in
    "hpe")
        regex_user="$regex_hpe"
        shortname_user="$shortname_hpe"
        ;;
    "intel")
        regex_user="$regex_intel"
        shortname_user="$shortname_intel"
        ;;
    "google")
        regex_user="$regex_google"
        shortname_user="$shortname_google"
        ;;
    "enakta")
        regex_user="$regex_enakta"
        shortname_user="$shortname_enakta"
        ;;
    *)
        regex_user="$regex_hpe"
        shortname_user="$shortname_hpe"
        ;;
esac

# Generate list of all copyright regex except the user's domain.
# Used to add a new copyright header to files.
all_regex_except_user=()
for _regex in "$regex_intel" "$regex_hpe"; do
    if [[ "$_regex" != "$regex_user" ]]; then
        all_regex_except_user+=("$_regex")
    fi
done


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

    # Check for existing copyright in user's domain
    # If it exists and is updated, nothing to do
    read -r y1_user y2_user <<< "$(sed -nre "s/^.*$regex_user.*$/\4 \6/p" "$file")"
    if [[ $y1_user -eq $year ]] || [[ $y2_user -eq $year ]]; then
        continue
    fi

    # If user's domain copyright exists but is outdated, it needs to be updated
    if [[ -n $y1_user ]] ; then
        if [[ "$mode" == "githook" ]]; then
            # Update copyright in place
            git_reset "$file" || continue
            if [[ "$os" == 'Linux' ]]; then
                sed -i -re "s/$regex_user/\1Copyright $y1_user-$year \8/" "$file"
            else
                sed -i '' -re "s/$regex_user/\1Copyright $y1_user-$year \8/" "$file"
            fi
            git_add "$file" || continue
        elif [[ "$mode" == "gha" ]]; then
            # Print error but do not update
            lineno="$(grep -nE "$regex_user" "$file" | cut -f1 -d:)"
            echo "::error file=$file,line=$lineno::Copyright out of date"
            errors=$((errors + 1))
        fi
        continue
    fi

    # User domain copyright does not exist so add it after an existing copyright
    did_add_copyright=false
    for _regex in "${all_regex_except_user[@]}"; do
        read -r y1_other y2_other <<< "$(sed -nre "s/^.*$_regex.*$/\4 \6/p" "$file")"
        if [[ -z $y1_other ]] ; then
            continue
        fi

        if [[ "$mode" == "githook" ]]; then
            # Add copyright in place, mimicking the format of existing copyright
            git_reset "$file" || continue
            if [[ -z "$y2_other" ]]; then
                y1_y2_other="$y1_other"
            else
                y1_y2_other="$y1_other-$y2_other"
            fi
            if [[ "$os" == 'Linux' ]]; then
                sed -i -re "s/$_regex/\1Copyright $y1_y2_other \8\n\1Copyright $year $shortname_user/" "$file"
            else
                sed -i '' -re "s/$_regex/\1Copyright $y1_y2_other \8\n\1Copyright $year $shortname_user/" "$file"
            fi
            git_add "$file" || continue
        elif [[ "$mode" == "gha" ]]; then
            # Print error but do not add
            lineno="$(grep -nE "$_regex" "$file" | cut -f1 -d:)"
            echo "::error file=$file,line=$lineno::Copyright out of date"
            errors=$((errors + 1))
        fi

        did_add_copyright=true
        break
    done

    if ! $did_add_copyright; then
        # Print warning but don't error on non-existent copyright since it's not easy to
        # determine the format and where to put it
        echo "  Copyright Information not found in: $file"
    fi
done

if [[ $errors -ne 0 ]]; then
    echo "  $errors errors while checking/fixing copyrights."
    exit 1
fi
