#!/bin/bash

pr_num() {
    # shellcheck disable=SC2153
    IFS=/ read -r -a github_ref <<< "$GITHUB_REF"
    echo "${github_ref[2]}"
}

# TODO: this should produce a JUnit result
error_exit() {
    echo "$1"
    exit 1
}

get_repo_serial() {
    # shellcheck disable=SC2153
    local serial_file="$REPO_PATH"/serial
    local serial=0
    if [ -f  "$serial_file" ]; then
        if ! read -r serial < "$serial_file"; then
            error_exit "Failed to read serial from $serial_file"
        fi
    fi

    echo "$serial"
}

get_repo_path() {
    local serial_file=$REPO_PATH/serial
    local serial
    serial=$(get_repo_serial)
    mkdir -p "$REPO_PATH"
    echo "$serial" > "$serial_file"
    repo_path="$REPO_PATH$serial/"
    mkdir -p "$repo_path"

    echo "$repo_path"

}

repo_serial_increment() {
    local serial_file=$REPO_PATH/serial
    local serial
    serial=$(get_repo_serial)
    ((serial++)) || true
    mkdir -p "$REPO_PATH"
    echo "$serial" > "$serial_file"
    repo_path="$REPO_PATH$serial/"
    mkdir -p "$repo_path"

    echo "$repo_path"
}