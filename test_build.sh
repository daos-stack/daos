#!/bin/sh

set -e
set -x

option=
if [ -n "$WORKSPACE" ]; then
export JOB_LOC=${WORKSPACE}/${JOB_NAME}/${BUILD_NUMBER}
export SRC_PREFIX_OPT="SRC_PREFIX=../"
else
export JOB_LOC=`pwd`/testbuild
mkdir -p $JOB_LOC
fi

# full build
./build_components.sh
./test_components.sh

# incremental build
./build_components.sh
./test_components.sh
