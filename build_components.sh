#!/bin/sh

set -e
set -x

option=
if [ -n "$JOB_LOC" ]; then
    export option="TARGET_PREFIX=${JOB_LOC} ${SRC_PREFIX_OPT}"
elif [ -n "$WORKSPACE" ]; then
    export option="TARGET_PREFIX=\
/scratch/coral/artifacts/${JOB_NAME}/${BUILD_NUMBER} SRC_PREFIX=../"
fi

/bin/rm -f *.conf
scons $option --config=force

if [ -n "$WORKSPACE" ]; then
if [ -z "$JOB_LOC" ]; then
    ln -sfn ${BUILD_NUMBER} /scratch/coral/artifacts/${JOB_NAME}/latest
fi
fi
