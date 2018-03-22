#!/bin/bash

set -uex

# Need a target and a target list.
# We can get that from the Jenkins JOB name.
# This code assumes no folders and a Matrix job.
job_real_name=${JOB_NAME%/*}
: ${TARGET:="${job_real_name%-*}"}
export TARGET
: ${TARGET_LIST:="${TARGET}"}
export TARGET_LIST
: ${REPO_LIST:="${TARGET}"}
export REPO_LIST

# Where we want the build to appear to be at in the container
: ${DIST_MOUNT="/testbin"}
export DIST_MOUNT
export DIST_TARGET="${DIST_MOUNT}"


# Where the build directory actually exists in the workspace
export WORK_TARGET="${PWD}/dist_target"
rm -rf ${WORK_TARGET}
mkdir -p ${WORK_TARGET}


# need to select the docker image.
# The distro environment variable is set by JENKINS for a matrix build
case ${distro} in
  el7.2)
    DOCKER_IMAGE="centos_7.2.1511_builder"
    ;;
  el7.3)
    DOCKER_IMAGE="centos_7.3.1611_builder"
    ;;
  el7|el7*)
    DOCKER_IMAGE="centos_7.4.1708_builder"
    ;;
  sles12sp3)
    DOCKER_IMAGE="sles_12.3_builder"
    ;;
  sles12|sles12*)
    DOCKER_IMAGE="sles_12.2_builder"
    ;;
  ubuntu14*)
    DOCKER_IMAGE="ubuntu_14.04.5_builder"
    ;;
  ubuntu16*)
    DOCKER_IMAGE="ubuntu_16.04_builder"
    ;;
esac
export DOCKER_IMAGE

# Docker can not see environment variables in the Jenkins workspace
# So provide a helper file to provide them.
docker_setup_file="docker_setup.sh"
rm -f ${docker_setup_file}
touch ${docker_setup_file}

# Export the build number
printf "export BUILD_NUMBER=${BUILD_NUMBER}\n" >> \
    ${docker_setup_file}
set -e
chmod 755 ${docker_setup_file}

set +u
# Pass the klocwork environment variables to the container.
if [ -n "${KLOCWORK_PROJECT}" ]; then
  printf "export KLOCWORK_PROJECT=${KLOCWORK_PROJECT}\n" >> \
    ${docker_setup_file}
fi
if [ -n "${KLOCWORK_LICENSE_HOST}" ]; then
  printf "export KLOCWORK_LICENSE_HOST=${KLOCWORK_LICENSE_HOST}\n" >> \
    ${docker_setup_file}
fi
if [ -n "${KLOCWORK_LICENSE_PORT}" ]; then
  printf "export KLOCWORK_LICENSE_PORT=${KLOCWORK_LICENSE_PORT}\n" >> \
    ${docker_setup_file}
fi
if [ -n "${KLOCWORK_URL}" ]; then
  printf "export KLOCWORK_URL=${KLOCWORK_URL}\n" >> \
    ${docker_setup_file}
fi

if [ -n "${KW_PATH}" ]; then
  printf "export KW_PATH=${KW_PATH}\n" >> \
    ${docker_setup_file}
  printf "export PATH=${KW_PATH}/bin:${PATH}\n" >> \
    ${docker_setup_file}
fi
set -u

# Look for extra container setup for the package
if [ -e extra_exports.sh ]; then
  cat extra_exports.sh >> ${docker_setup_file}
fi

