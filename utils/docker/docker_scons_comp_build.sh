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

scons ${1} ${2}
scons install

if [ -n "${3}" ]; then
  if [ -e "${3}" ]; then
    #custom build step
    print_status "Running custom build step"
    COMP_PREFIX=${4}
    source ${3}
  fi
fi

