#!/bin/bash

# Post build task to save artifacts to an NFS managed directory
# to allow build and CI jobs to quickly access artifacts.
#
# This allows build jobs to be located by a commit hash the build
# is based on.
#
# COMMIT_REPO designates which *_git_commit file use for the commit
# hash to be used.  It defaults to the prefix of the ${JOB_NAME} set
# by Jenkins, which handles most cases.
#
# Some jobs like mpi4py need to be accessed by the ompi commit hash
# the paramter p1 allows specifying an alternate commit file.
#
# A git commit hash symbolic link only set if the expected *_git_commit
# file is found.
#
# CORAL_ARTIFACTS should be set by the Jenkins system administrator.

set -x

param="${1}"

# if param is not "nosymlink", then it is a git repository name.
if [ ! "${param}" != "nosymlink" ]; then
  COMMIT_REPO="${param}"
fi

: "${COMMIT_REPO:=${JOB_NAME%%-*}}"

job_artifact="${CORAL_ARTIFACTS}/${JOB_NAME}"

build_artifact="${job_artifact}/${BUILD_NUMBER}"

# Archve anything in workspace artifacts directory to CORAL_ARTIFACTS
if [ -n "$(ls -A "${WORKSPACE}/artifacts")"  ];then
  # shellcheck disable=SC2174
  mkdir -p "${build_artifact}" -m 775

  cp -r "${WORKSPACE}"/artifacts/* "${build_artifact}"
  chmod -R u=rwX,g=rwX,o=rX "${build_artifact}"
fi

# If we do not need symbolic links to builds, we are done.
if [ "${param}" == "nosymlink" ]; then
  exit 0
fi

# Update the link to the latest artifact.
find "${CORAL_ARTIFACTS}/${JOB_NAME}" -maxdepth 2 -name latest \
  -exec rm {} \; -quit

find "${CORAL_ARTIFACTS}/${JOB_NAME}" -maxdepth 2 -name "${BUILD_NUMBER}" \
  -exec ln -s {} {}/../latest \; -quit

# Optionaly create a GIT commit hash link.
if [ -e "${build_artifact}/${COMMIT_REPO}_git_commit" ]; then
  commit=$(cat "${build_artifact}/${COMMIT_REPO}_git_commit")

  # ln -f not working reliably on NFS volume.
  if [ -L "${job_artifact}/${commit}" ]; then
    rm "${job_artifact}/${commit}"
  fi
  ln -f -s "${build_artifact}" "${job_artifact}/${commit}"
fi
# Optionally create a GIT tag link.
if [ -e "${build_artifact}/${COMMIT_REPO}_git_tag" ]; then
  commit=$(cat "${build_artifact}/${COMMIT_REPO}_git_tag")

  # ln -f not working reliably on NFS volume.
  if [ -L "${job_artifact}/${commit}" ]; then
    rm "${job_artifact}/${commit}"
  fi
  ln -f -s "${build_artifact}" "${job_artifact}/${commit}"
fi
find -L "${job_artifact}" -maxdepth 2 -type l -delete || true

