#!/bin/bash

set -uex

docker_pre_script=`find . -name docker_host_prerun.sh`
docker_post_script=`find . -name docker_host_postrun.sh`

: ${JOB_SUFFIX:="-update-scratch"}

# Review jobs need to run the pre script once before this script
# and not re-run it.
if [[ ! -v DOCKER_IMAGE ]];then
  source ${docker_pre_script}
else
  docker_setup_file="docker_setup.sh"
fi

default_requires=

job_real_name=${JOB_NAME%/*}
if [[ "${job_real_name}" == *${JOB_SUFFIX} ]];then
  default_requires="REQUIRES=${TARGET}"
fi

set +u
if [ -n "${PREFIX}" ]; then
  options="option=\"PREFIX=${PREFIX}"
else
  options="option=\"TARGET_PREFIX=${DIST_TARGET}"
fi
if [ -n "${PREBUILT_PREFIX}" ]; then
  options="${options} PREBUILT_PREFIX=${PREBUILT_PREFIX}"
fi
set -u

: ${SCONS_OPTIONS:="--build-deps=yes --config=force"}

printf "${options} SRC_PREFIX=/work\"\n" >> ${docker_setup_file}

# Two possible setups:
# 1. scons_local is submodule of target
# 2. scons_local is in a subdirectory of workspace
if [ -d scons_local ];then
  scons_local_dir="/work/scons_local"
else
  scons_local_dir="/work/${TARGET}/scons_local"
fi

docker run --rm -u $USER -v ${PWD}:/work \
           -v ${WORK_TARGET}:${DIST_MOUNT} \
           -a stderr -a stdout -i coral/${DOCKER_IMAGE} \
           ${scons_local_dir}/utils/docker/docker_build_components.sh \
          ${default_requires} ${SCONS_OPTIONS} 2>&1 | tee docker_build.log

# Review jobs do not have artifacts to process
if [[ "${job_real_name}" == *${JOB_SUFFIX} ]];then
  source ${docker_post_script}
fi

set +u
target_post_build=`find . -name ${TARGET}_post_build.sh`

if [ -n "${target_post_build}" ]; then
  source ${target_post_build}
fi
set -u

