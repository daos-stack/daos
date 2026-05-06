#!/bin/bash

# Script for building DAOS
# If the STAGE_NAME environment variable is present the script will
# attempt to parse it to determine what distribution to build for.
#
# For manual testing, you can set the environment variables CHROOT_NAME
# and TARGET can be set.
#
# Default is to build for CentOS 7.
# Fault injection will be enabled by default in CI unless a pragma has
# has disabled fault injection or this is a Release build
set -uex

# Default values for variables that may be set in the environment
: "${JOBS:=144}"
: "${COMPILER:=gcc}"
: "${DAOS_BUILD_TYPE:=dev}"
: "${DAOS_TARGET_TYPE:=release}"

scons --jobs "$JOBS" --build-deps=no install PREFIX=/opt/daos COMPILER="$COMPILER" \
      FIRMWARE_MGMT=1 BUILD_TYPE="$DAOS_BUILD_TYPE" TARGET_TYPE="$DAOS_TARGET_TYPE" \
      USE_INSTALLED=all
