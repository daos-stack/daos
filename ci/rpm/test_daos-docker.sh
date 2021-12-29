#!/bin/bash

set -uex

add_repo() {
    local REPO_URL="$1"

    sudo dnf config-manager --add-repo="$REPO_URL"
    repo=${REPO_URL#*://}
    repo="${repo//%/}"
    repo="${repo//\//_}"
    sudo dnf config-manager --save --setopt="$repo".gpgcheck=0
}

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

PR_REPOS="$1"
if [ -n "$PR_REPOS" ]; then
    for repo in $PR_REPOS; do
        branch="master"
        build_number="lastSuccessfulBuild"
        if [[ $repo = *@* ]]; then
            branch="${repo#*@}"
            repo="${repo%@*}"
            if [[ $branch = *:* ]]; then
                build_number="${branch#*:}"
                branch="${branch%:*}"
            fi
        fi
        add_repo "${JENKINS_URL}job/daos-stack/job/$repo/job/$branch/$build_number/artifact/artifacts/centos8/"
    done
elif $ADD_JENKINS_REPO; then
    add_repo "${BUILD_URL}artifact/artifacts/centos8/"
fi

MODULEPATH=/usr/share/Modules/modulefiles:/etc/modulefiles:/usr/share/modulefiles \
DAOS_PKG_VERSION="$2" OFI_INTERFACE="lo" "$mydir"/test_daos_node.sh
