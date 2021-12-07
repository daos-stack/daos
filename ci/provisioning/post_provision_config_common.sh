#!/bin/bash

set -eux

repo_server_pragma=$(echo "$COMMIT_MESSAGE" | sed -ne '/^Repo-servers: */s/.*: *//p')
if [ -n "$repo_server_pragma" ]; then
    IFS=" " read -r -a repo_servers <<< "$repo_server_pragma"
else
    # default is artifactory
    # shellcheck disable=SC2034
    repo_servers=('artifactory' 'nexus')
fi
