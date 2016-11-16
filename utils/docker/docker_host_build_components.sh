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

printf "option=\"TARGET_PREFIX=${DIST_TARGET} SRC_PREFIX=../\"\n" >> \
  ${docker_setup_file}

docker run --rm -u $USER -v ${PWD}:/work \
           -v ${WORK_TARGET}:${DIST_MOUNT} \
           -a stderr -a stdout -i coral/${DOCKER_IMAGE} \
           /work/scons_local/utils/docker/docker_build_components.sh \
	   ${default_requires} 2>&1 | tee docker_build.log

# Review jobs do not have artifacts to process
if [ "${job_real_name}" == *${JOB_SUFFIX} ];then
  source ${docker_post_script}
fi

set +u
target_post_build=`find . -name ${TARGET}_post_build.sh`

if [ "${target_post_build}x" != "x" ]; then
  source ${target_post_build}
fi
set -u

