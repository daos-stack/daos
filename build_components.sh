#!/bin/sh

set -e
set -x

option=
if [ -n "$WORKSPACE" ]; then
    export option="TARGET_PREFIX=\
/scratch/coral/artifacts/${JOB_NAME}/${BUILD_NUMBER}"
fi

/bin/rm -f *.conf
scons $option SRC_PREFIX=../

if [ -n "$WORKSPACE" ]; then
    ln -sfn ${BUILD_NUMBER} /scratch/coral/artifacts/${JOB_NAME}/latest
fi
