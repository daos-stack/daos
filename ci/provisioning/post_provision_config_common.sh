#!/bin/bash

set -eux

repo_server_pragma=$(echo "$COMMIT_MESSAGE" | sed -ne '/^Repo-server: */s/.*: *//p')
if [ -n "$repo_server_pragma" ]; then
    IFS=" " read -r -a repo_servers <<< "$repo_server_pragma"
else
    # default is artifactory
    repo_servers=(artifactory nexus)
fi

# Update the repo files
for repo_server in "${repo_servers[@]}"; do
    if ! fetch_repo_config "$repo_server"; then
        # leave the existing on-image repo config alone if the repo fetch fails
        send_mail "Fetch repo file for repo server \"$repo_server\" failed.  Continuing on with in-image repos."
    fi
done
time dnf -y repolist

if ! curl -o /usr/local/sbin/set_local_repos.sh-tmp "${REPO_FILE_URL}set_local_repos.sh"; then
    send_mail "Fetch set_local_repos.sh failed.  Continuing on with in-image copy."
else
    cat /usr/local/sbin/set_local_repos.sh-tmp > /usr/local/sbin/set_local_repos.sh
    rm -f /usr/local/sbin/set_local_repos.sh-tmp
fi

set_local_repos.sh "${repo_servers[0]}"
