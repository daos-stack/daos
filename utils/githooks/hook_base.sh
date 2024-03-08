#!/bin/bash
#
#  Copyright 2024 Intel Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -eu

. utils/githooks/find_base.sh
export TARGET

# Common function to keep headers aligned
function _print_githook_header() {
    printf "%-17s " "${1}:"
}
export -f _print_githook_header

# Get list of staged files, excluding deleted
# shellcheck disable=SC2086
function _git_diff_cached_files() {
    IFS=' ' read -r -a _filter <<< "${1:-}"
    local args="${2:-}"
    if [ ${#_filter[@]} -eq 0 ]; then
        git diff "$TARGET" --cached --name-only --diff-filter=d $args
    else
        git diff "$TARGET" --cached --name-only --diff-filter=d $args -- "${_filter[@]}"
    fi
}
export -f _git_diff_cached_files

hook=${0##*/}
rm -f ".${hook}"

IFS=', ' read -r -a skip_list <<< "${DAOS_GITHOOK_SKIP:-}"

run-parts() {
    local dir="$1"
    shift

    for i in $(LC_ALL=C; echo "${dir%/}"/*[^~,]); do
        # don't run vim .swp files
        [ "${i%.sw?}" != "${i}" ] && continue
        # for new repo, skip old changeId script
        [ $(basename "${i}") == "20-user-changeId" ] && continue
        skip_item=false
        for skip in "${skip_list[@]}"; do
            if [[ "${i}" =~ ${skip} ]]; then
                skip_item=true
                echo "Skipping ${i}"
                break
            fi
        done
        $skip_item && continue
        $i "$@"
    done
}

run-parts utils/githooks/"${hook}".d "$@" 1>&2

# Create temp file for the commit-msg watermark to indicate this hook was ran.
# But not for the commit-msg itself.
if [ "${hook}" != "commit-msg" ]; then
    touch ".${hook}"
fi
