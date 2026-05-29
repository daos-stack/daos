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

scons -c && \
rm -rf _build.external install build daos_m.conf daos.conf iof.conf\
      cart-Linux.conf .sconsign.dblite .sconsign-Linux.dblite .sconf-temp .sconf-temp-Linux && \
scons --config=force --jobs "$JOBS" --build-deps=no install PREFIX="$PREFIX" COMPILER="$COMPILER" \
      BUILD_TYPE="$DAOS_BUILD_TYPE" TARGET_TYPE="$DAOS_TARGET_TYPE" \
      USE_INSTALLED=all | tee $WORKSPACE/${TARGET}-${COMPILER}-build.log
