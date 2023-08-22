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

cancel_provision() {
    trap 'rm -f $cookiejar' EXIT
    cookiejar="$(mktemp)"
    # shellcheck disable=SC2153
    crumb="$(curl --cookie-jar "$cookiejar" \
             "${JENKINS_URL}crumbIssuer/api/xml?xpath=concat(//crumbRequestField,\":\",//crumb)")"
    # shellcheck disable=SC2001
    url="$(echo "$QUEUE_URL" | sed -e 's|item/\(.*\)/|cancelItem?id=\1|')"
    trap 'set -x; rm -f $cookiejar $headers_file' EXIT
    headers_file="$(mktemp)"
    if ! curl -D "$headers_file" -v -f -X POST --cookie "$cookiejar" -H "$crumb" "$url"; then
        echo "Failed to cancel cluster provision request."
        cat "$headers_file"
    fi
}