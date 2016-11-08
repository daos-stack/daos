#!/bin/bash

set -uex

docker_pre_script=`find . -name docker_host_prerun.sh`
docker_post_script=`find . -name docker_host_postrun.sh`

source ${docker_pre_script}

# Look up the original comp_build.sh script expected to be used.
comp_build1=`grep comp_build.sh -r --include="build_${TARGET}.sh"`
comp_build2=${comp_build1#*:}
IFS=' ' read -a comp_build3 <<<"${comp_build2}"
comp_build_script=${comp_build3[0]}

# Actually run the file
${comp_build_script} $*

source ${docker_post_script}

set +u
target_post_build=`find . -name ${TARGET}_post_build.sh`

if [ "${target_post_build}x" != "x" ]; then
  source ${target_post_build}
fi
set -u
