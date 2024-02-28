#!/bin/bash
set -eu

. utils/githooks/find_base.sh
export TARGET

hook=${0##*/}
rm -f ".${hook}"

IFS=', ' read -r -a skip_list <<< "${DAOS_GITHOOK_SKIP:-}"

run-parts() {
    local dir="$1"
    shift

    for i in $(LC_ALL=C; echo "${dir%/}"/*[^~,]); do
        # don't run vim .swp files
        [ "${i%.sw?}" != "${i}" ] && continue
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

touch ".${hook}"
