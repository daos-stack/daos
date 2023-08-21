#!/bin/bash

set -eux

repo_server_pragma=$(echo "$COMMIT_MESSAGE" | sed -ne '/^Repo-servers: */s/.*: *//p')
if [ -n "$repo_server_pragma" ]; then
    IFS=" " read -r -a repo_servers <<< "$repo_server_pragma"
else
    # default is artifactory
    # shellcheck disable=SC2034
    repo_servers=('artifactory')
fi

# Use a daos-do/repo-files PR if specified
repo_files_pr=$(echo "$COMMIT_MESSAGE" | sed -ne '/^Repo-files-PR: */s/.*: *//p')
if [ -n "$repo_files_pr" ]; then
    build_number="lastSuccessfulBuild"
    branch="$repo_files_pr"
    if [[ $branch = *:* ]]; then
        build_number="${branch#*:}"
        branch="${branch%:*}"
    fi
    # shellcheck disable=SC2034
    REPO_FILE_URL="${JENKINS_URL:-https://build.hpdd.intel.com/}job/daos-do/job/repo-files/job/$branch/$build_number/artifact/"
fi

. /etc/os-release
# shellcheck disable=SC2034
EXCLUDE_UPGRADE=mercury,daos,daos-\*
if rpm -qa | grep mlnx; then
    # packages not to allow upgrading if MLNX OFED is installed
    EXCLUDE_UPGRADE+=,openmpi,\*mlnx\*,\*ucx\*
fi
case "$ID_LIKE" in
    *rhel*)
        if [ "$VERSION_ID" = "7" ]; then
            DISTRO_NAME=centos"$VERSION_ID"
            EXCLUDE_UPGRADE+=,fuse
        else
            DISTRO_NAME=el${VERSION_ID%%.*}
            EXCLUDE_UPGRADE+=,dpdk\*
        fi
        REPOS_DIR=/etc/yum.repos.d
        DISTRO_GENERIC=el
        ;;
    *suse*)
        # shellcheck disable=SC2034
        DISTRO_NAME=leap${VERSION_ID%%.*}
        # shellcheck disable=SC2034
        DISTRO_GENERIC=sl
        # shellcheck disable=SC2034
        REPOS_DIR=/etc/dnf/repos.d
        EXCLUDE_UPGRADE+=,fuse,fuse-libs,fuse-devel
        ;;
esac

# shellcheck disable=SC2034
MLNX_VER_NUM=5.8-1.1.2.1
