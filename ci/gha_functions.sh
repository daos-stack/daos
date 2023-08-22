#!/bin/bash

# TODO: this should produce a JUnit result
error_exit() {
    echo "$1"
    exit 1
}

get_repo_path() {
    # shellcheck disable=SC2153
    local repo_path="$REPO_PATH$GITHUB_RUN_NUMBER/artifact/artifacts/"
    mkdir -p "$repo_path"

    echo "$repo_path"

}

cleanup_provision_request () {
    local reqid="$1"
    if ! rm -f /scratch/Get\ a\ cluster/"$reqid"; then
       id;
       ls -l /scratch/Get\ a\ cluster/"$reqid";
    fi;
}