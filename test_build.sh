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

test_build()
{
if [ -n "$WORKSPACE" ]; then
export prebuilt="PREBUILT_PREFIX=$JOB_LOC/pmix \
                PMIX_PREBUILT=$JOB_LOC/pmix"
else
export prebuilt="HWLOC_PREBUILT=$JOB_LOC/hwloc PMIX_PREBUILT=$JOB_LOC/pmix"
fi
scons $prebuilt -C test --config=force --update-prereq=all --build-deps=yes

#This one defines pmix2
scons $prebuilt -C test -f SConstruct.alt --config=force --build-deps=yes

./test_components.sh
}

# incremental build
./build_components.sh
test_build

# full build
rm -rf _build.external build install
./build_components.sh
test_build

