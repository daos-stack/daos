#!/bin/bash

# TODO: this should produce a JUnit result
error_exit() {
    echo "$1"
    exit 1
}

get_repo_path() {
    repo_path="$REPO_PATH$GITHUB_RUN_NUMBER/artifact/artifacts/"
    mkdir -p "$repo_path"

    echo "$repo_path"

}
