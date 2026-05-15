#!/bin/bash

# Script for building DAOS
#
# Default is to build for Rocky 9.
set -uex

# Default values for variables that may be set in the environment
echo "Get number of processors online"
: "${JOBS:=$(nproc)}"
: "${COMPILER:=gcc}"
: "${DAOS_BUILD_TYPE:=dev}"
: "${DAOS_TARGET_TYPE:=release}"
: "${TARGET:=el9}"
: "${PREFIX:=/opt/daos}"

echo "Remove some old build files if present."
rm -rf src/rdb/raft/CLinkedListQueue bandit.xml test.cov

# Original code from Jenkinsfile, moved here to be used in both Jenkins and manual builds
# command -v scons
#
# /usr/bin/getconf _NPROCESSORS_ONLN
# echo "Get number of processors online"
# rm -rf src/rdb/raft/CLinkedListQueue bandit.xml test.cov
# echo "Remove some old build files if present."
# String tee_file = '| tee $WORKSPACE/' + stage_info['log_to_file']
# /home/daos/venv/bin/scons -c
# rm -rf _build.external install build daos_m.conf daos.conf iof.conf cart-Linux.conf .sconsign.dblite .sconsign-Linux.dblite .sconf-temp .sconf-temp-Linux
# SCONS_ARGS='-j 144 --build-deps=no install USE_INSTALLED=all COMPILER=gcc BUILD_TYPE=dev PREFIX=/opt/daos TARGET_TYPE=release'
# /home/daos/venv/bin/scons --config=force -j 144 --build-deps=no install USE_INSTALLED=all COMPILER=gcc BUILD_TYPE=dev PREFIX=/opt/daos TARGET_TYPE=release
# tee /var/lib/jenkins/jenkins-3/docker_1/workspace/daos-stack_daos_master@2/el8-gcc-build.log


scons -c && \
rm -rf _build.external install build daos_m.conf daos.conf iof.conf\
      cart-Linux.conf .sconsign.dblite .sconsign-Linux.dblite .sconf-temp .sconf-temp-Linux && \
scons --config=force --jobs "$JOBS" --build-deps=no install PREFIX="$PREFIX" COMPILER="$COMPILER" \
      BUILD_TYPE="$DAOS_BUILD_TYPE" TARGET_TYPE="$DAOS_TARGET_TYPE" \
      USE_INSTALLED=all | tee $WORKSPACE/${TARGET}-${COMPILER}-build.log
