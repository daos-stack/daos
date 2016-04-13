#!/bin/sh

set -e
set -x

option=
if [ -n "$JOB_LOC" ]; then
    export option="TARGET_PREFIX=${JOB_LOC} ${SRC_PREFIX_OPT}"
elif [ -n "$WORKSPACE" ]; then
    export option="TARGET_PREFIX=\
${CORAL_ARTIFACTS}/${JOB_NAME}/${BUILD_NUMBER} SRC_PREFIX=../"
fi

scons $option --config=force --update-prereq=all --build-deps=yes $*

if [ -n "$WORKSPACE" ]; then
if [ -z "$JOB_LOC" ]; then
    ln -sfn ${BUILD_NUMBER} ${CORAL_ARTIFACTS}/${JOB_NAME}/latest
fi
fi
