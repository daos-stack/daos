#!/bin/bash

set -uex

docker_pre_script=$(find . -name docker_host_prerun.sh -print -quit)
docker_post_script=$(find . -name docker_host_postrun.sh -print -quit)

: "${JOB_SUFFIX:="-update-scratch"}"

# Review jobs need to run the pre script once before this script
# and not re-run it.
if [[ ! -v DOCKER_IMAGE ]];then
  # shellcheck disable=SC1090
  source "${docker_pre_script}"
else
  docker_setup_file="docker_setup.sh"
fi

job_real_name=${JOB_NAME%/*}
if [[ "${job_real_name}" == *${JOB_SUFFIX} ]];then
 : "${DEFAULT_REQUIRES="REQUIRES=${TARGET}"}"
else
 : "${DEFAULT_REQUIRES=""}"
fi

set +u
if [ -n "${PREFIX}" ]; then
  options="option=\"PREFIX=${PREFIX}"
else
  options="option=\"TARGET_PREFIX=${DIST_TARGET}"
fi
if [ -n "${PREBUILT_PREFIX}" ]; then
  options+=" PREBUILT_PREFIX=${PREBUILT_PREFIX}"
fi
set -u

: "${SCONS_OPTIONS:="--build-deps=yes --config=force"}"

printf "%s SRC_PREFIX=/work\"\n" "${options}" >> ${docker_setup_file}

# Two possible setups:
# 1. scons_local is submodule of target
# 2. scons_local is in a subdirectory of workspace
if [ -d scons_local ];then
  scons_local_dir="/work/scons_local"
else
  scons_local_dir="/work/${TARGET}/scons_local"
fi

: "${DOCKER_OPTIONS=""}"

# Matrix must be a unique id, but has a restricted character set.
# We want it to be informative for debug.
# shellcheck disable=SC2154
matrix="${distro%.*}_$$"

echo "${job_real_name}_${BUILD_NUMBER}_${matrix}" > \
  "${WORKSPACE}/docker_container_name.txt"

kwmount=""
# Need to mount the klocwork tools path for the docker container
# The build agent should set the KW_PATH environment variable.
set +u
if [ -n "${KW_PATH}" ]; then
  kwmount="-v ${KW_PATH}:${KW_PATH}"
  kwmount+=" -v ${KW_SETTINGS}:${KW_SETTINGS}"
fi
set -u

# shellcheck disable=2086
docker run --rm --name "${job_real_name}_${BUILD_NUMBER}_${matrix}" \
           -u "${USER}" -v "${PWD}":/work ${kwmount} \
           -v "${WORK_TARGET}:${DIST_MOUNT}" \
           --privileged --device /dev/fuse:/dev/fuse:rwm \
           -a stderr -a stdout ${DOCKER_OPTIONS} \
           -i "coral/${DOCKER_IMAGE}" \
           "${scons_local_dir}/utils/docker/docker_build_components.sh" \
          ${DEFAULT_REQUIRES} ${SCONS_OPTIONS} 2>&1 | tee docker_build.log

# Sometimes a docker script exits normally, but the docker container is not
# automatically deleted.  This attempts to force a cleanup.
sleep 1
set +e
docker ps | grep "${job_real_name}_${BUILD_NUMBER}_${matrix}"
if [ $? -eq 0 ]; then
  docker rm -f "${job_real_name}_${BUILD_NUMBER}_${matrix}"
fi
set -e

docker_exit_status=0
# Some jobs used to not need artifacts based on JOB_SUFFIX.
# Review jobs feeding maloo need artifacts.
if [[ "${job_real_name}" == *${JOB_SUFFIX} ]]; then
  : "${NEED_ARTIFACTS="1"}"
else
  : "${NEED_ARTIFACTS=""}"
fi

if [ -n "${NEED_ARTIFACTS}" ]; then
  # shellcheck disable=SC1090
  source "${docker_post_script}"
  docker_exit_status=$?
fi

set +u
target_post_build=$(find . -name "${TARGET}_post_build.sh" -print -quit)
if [ -n "${target_post_build}" ]; then
  # shellcheck disable=SC1090
  source "${target_post_build}"
  if [ $? -ne 0 ]; then
    docker_exit_status=$?
  fi
fi
set -u

set +ue
if [ -z "${IGNORE_SCONS_ERRORS}" ];then
   grep -q "scons: done building targets" ./docker_build.log
   if [ $? -ne 0 ];then
     docker_exit_status=1
   fi
   grep -q "scons: building terminated because of errors" ./docker_build.log
   if [ $? -eq 0 ];then
     docker_exit_status=1
   fi
fi
set -xe
exit ${docker_exit_status}

