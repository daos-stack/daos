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

id=$(lsb_release -si)
release=$(lsb_release -sr)
# shellcheck disable=SC2034
case "$id" in
    CentOS|Rocky|AlmaLinux|RedHatEnterpriseServer)
        if [ "${release%%.*}" = 7 ]; then
            DISTRO_NAME=centos${release%%.*}
        else
            DISTRO_NAME=el${release%%.*}
        fi
        REPOS_DIR=/etc/yum.repos.d
        DISTRO_GENERIC=el
        LSB_RELEASE=redhat-lsb-core
        EXCLUDE_UPGRADE=fuse,mercury,daos,daos-\*
        if [ "${release%%.*}" = 8 ]; then
            EXCLUDE_UPGRADE=dpdk,$EXCLUDE_UPGRADE
        fi
        ;;
    openSUSE)
        DISTRO_NAME=leap${release%%.*}
        DISTRO_GENERIC=sl
        REPOS_DIR=/etc/dnf/repos.d
        EXCLUDE_UPGRADE=fuse,fuse-libs,fuse-devel,mercury,daos,daos-\*
        ;;
esac
