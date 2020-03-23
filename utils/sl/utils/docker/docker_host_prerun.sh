#!/bin/bash

set -uex

# Need a target and a target list.
# We can get that from the Jenkins JOB name.
# This code assumes no folders and a Matrix job.
job_real_name=${JOB_NAME%/*}
: "${TARGET:="${job_real_name%-*}"}"
export TARGET
: "${TARGET_LIST:="${TARGET}"}"
export TARGET_LIST
: "${REPO_LIST:="${TARGET}"}"
export REPO_LIST

# Where we want the build to appear to be at in the container
: "${DIST_MOUNT="/testbin"}"
export DIST_MOUNT
export DIST_TARGET="${DIST_MOUNT}"


# Where the build directory actually exists in the workspace
export WORK_TARGET="${PWD}/dist_target"
rm -rf "${WORK_TARGET}"
mkdir -p "${WORK_TARGET}"


# need to select the docker image.
# The distro environment variable is set by JENKINS for a matrix build
# shellcheck disable=SC2154
case ${distro} in
  el7.2)
    DOCKER_IMAGE="centos_7.2.1511_builder"
    ;;
  el7.3)
    DOCKER_IMAGE="centos_7.3.1611_builder"
    ;;
  el7.4)
    DOCKER_IMAGE="centos_7.4.1708_builder"
    ;;
  el7|el7*)
    DOCKER_IMAGE="centos_7.5.1804_builder"
    ;;
  sles12|sles12*)
    DOCKER_IMAGE="sles_12.3_builder"
    ;;
  sles15|sles15*)
    DOCKER_IMAGE="sles_15.0_builder"
    ;;
  leap15|leap15*)
    DOCKER_IMAGE="leap_15.0_builder"
    ;;
  fedora)
    DOCKER_IMAGE="fedora_latest_builder"
    ;;
  ubuntu14*)
    DOCKER_IMAGE="ubuntu_14.04.5_builder"
    ;;
  ubuntu16*)
    DOCKER_IMAGE="ubuntu_16.04_builder"
    ;;
  ubuntu18*)
    DOCKER_IMAGE="ubuntu_18.04_builder"
    ;;
esac
export DOCKER_IMAGE

# Docker can not see environment variables in the Jenkins workspace
# So provide a helper file to provide them.
docker_setup_file="docker_setup.sh"
rm -f "${docker_setup_file}"
touch "${docker_setup_file}"

# Export the build number
printf "export BUILD_NUMBER=\"%s\"\n" "${BUILD_NUMBER}" >> \
    "${docker_setup_file}"
set -e
chmod 755 "${docker_setup_file}"

# Export the path to all artifacts
printf "export DIST_TARGET=\"%s\"\n" "${DIST_TARGET}" >> \
    "${docker_setup_file}"

set +u
# Pass the klocwork environment variables to the container.
if [ -n "${KLOCWORK_PROJECT}" ]; then
  printf "export KLOCWORK_PROJECT=\"%s\"\n" "${KLOCWORK_PROJECT}" >> \
    "${docker_setup_file}"
fi
if [ -n "${KLOCWORK_LICENSE_HOST}" ]; then
  printf "export KLOCWORK_LICENSE_HOST=$\"%s\"\n" "${KLOCWORK_LICENSE_HOST}" \
    >> "${docker_setup_file}"
fi
if [ -n "${KLOCWORK_LICENSE_PORT}" ]; then
  printf "export KLOCWORK_LICENSE_PORT=\"%s\"\n" "${KLOCWORK_LICENSE_PORT}" \
    >> "${docker_setup_file}"
fi
if [ -n "${KLOCWORK_URL}" ]; then
  printf "export KLOCWORK_URL=\"%s\"\n" "${KLOCWORK_URL}" >> \
    "${docker_setup_file}"
fi

if [ -n "${KW_PATH}" ]; then
  printf "export KW_PATH=\"%s\"\n" "${KW_PATH}" >> \
    "${docker_setup_file}"
  printf "export PATH=%s/bin:%s\n" "${KW_PATH}" "${PATH}" >> \
    "${docker_setup_file}"
fi
set -u

# Look for extra container setup for the package
if [ -e extra_exports.sh ]; then
  cat extra_exports.sh >> "${docker_setup_file}"
fi

# Clear out the old artifacts to prevent stale files if a build fails.
artifact_dest="${PWD}/artifacts/"
rm -rf "${artifact_dest}"


