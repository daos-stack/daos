#!/bin/bash

set -uex

docker_pre_script=`find . -name docker_host_prerun.sh`
docker_post_script=`find . -name docker_host_postrun.sh`

source ${docker_pre_script}

comp_build_script=`find . -name comp_build.sh`

# Actually run the file
${comp_build_script} $*

source ${docker_post_script}

set +u
target_post_build=`find . -name ${TARGET}_post_build.sh`

if [ "${target_post_build}x" != "x" ]; then
  source ${target_post_build}
fi
set -u
