#!/bin/sh
# general build script for CaRT

set -e
set -x

if [ ! -d "scons_local" ];then
  cd ..
fi

scons_local/comp_build.sh -c utils/build.config utils/run_test.sh
