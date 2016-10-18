#!/bin/bash

set -uex

docker_pre_script=`find . -name docker_host_prerun.sh`
docker_post_script=`find . -name docker_host_postrun.sh`

source ${docker_pre_script}

printf "option=\"TARGET_PREFIX=${DIST_TARGET} SRC_PREFIX=../\"\n" >> \
  ${docker_setup_file}

docker run --rm -u $USER -v ${PWD}:/work \
           -v ${WORK_TARGET}:${DIST_MOUNT} \
           -a stderr -a stdout -i coral/${DOCKER_IMAGE} \
           /work/scons_local/utils/docker/docker_build_components.sh \
	   REQUIRES=${TARGET} | tee docker_build.log

source ${docker_post_script}

set +u
target_post_build=`find . -name ${TARGET}_post_build.sh`

if [ "${target_post_build}x" != "x" ]; then
  source ${target_post_build}
fi
set -u

