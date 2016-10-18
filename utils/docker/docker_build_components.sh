#!/bin/bash

set -x
cd /work

docker_setup_file="/work/docker_setup.sh"
if [ -e ${docker_setup_file} ]; then
  source ${docker_setup_file}
fi

pushd scons_local
scons $option --config=force --update-prereq=all --build-deps=yes $*
popd

