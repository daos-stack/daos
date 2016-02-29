#!/bin/sh

set -e
set -x

option=
if [ -n "$WORKSPACE" ]; then
    export option="TARGET_PREFIX=$WORKSPACE/${JOB_NAME}/${BUILD_NUMBER}"
fi

/bin/rm -rf _build.external
/bin/rm -f *.conf
scons $option SRC_PREFIX=../

if [ -n "$WORKSPACE" ]; then
    ln -sfn ${BUILD_NUMBER} ${WORKSPACE}/${JOB_NAME}/latest
    export prebuilt="PREBUILT_PREFIX=$WORKSPACE/${JOB_NAME}/latest \
                    PMIX_PREBUILT=`pwd`/install"
else
    export prebuilt="HWLOC_PREBUILT=`pwd`/install PMIX_PREBUILT=`pwd`/install"
fi
scons $prebuilt -C test

#This one defines pmix2
scons $prebuilt -C test -f SConstruct.alt
