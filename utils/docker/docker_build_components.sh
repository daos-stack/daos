#!/bin/bash

set -x
cd /work

function print_status()
{
    echo "*******************************************************************"
    echo $*
    echo "*******************************************************************"
}

docker_setup_file="/work/docker_setup.sh"
if [ -e ${docker_setup_file} ]; then
  source ${docker_setup_file}
fi

if [ -e SConstruct ];then
  scons_local_dir="."
else
  scons_local_dir="scons_local"
fi

pushd ${scons_local_dir}
scons $option $*

if [ -n "$SCONS_INSTALL}" ];then
  scons install
fi
popd

set +e
if [ -n "${CUSTOM_BUILD_STEP}" ];then
  print_status "Running custom build step"
  source ${CUSTOM_BUILD_STEP}
fi
set -e

