#!/bin/sh

set -e
set -x

option=
if [ -n "$WORKSPACE" ]; then
prebuilt1=\
"PREBUILT_PREFIX=${CORAL_ARTIFACTS}/mercury-update-scratch/latest:\
${CORAL_ARTIFACTS}/ompi-update-scratch/latest"
prebuilt2=\
"HWLOC_PREBUILT=${CORAL_ARTIFACTS}/ompi-update-scratch/latest/hwloc \
OPENPA_PREBUILT=${CORAL_ARTIFACTS}/mercury-update-scratch/latest/openpa"
fi

mkdir -p test/prefix_test
rm -rf test/prefix_test/*
scons -C test -f SConstruct $prebuilt1 --build-deps=yes --config=force
scons -C test -f SConstruct $prebuilt2 --build-deps=yes --config=force
