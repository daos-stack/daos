#!/bin/bash

# Script for building RPMs in a chroot
# If the STAGE_NAME environment variable is present the script will
# attempt to parse it to determine what distribution to build for.
#
# For manual testing, you can set the environment variables CHROOT_NAME
# and TARGET can be set.
#
# Default is to build for CentOS 7.
# Fault injection will be enabled by default in CI unless a pragma has
# has disabled fault injection or this is a Release build
set -ex

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ci_envs="$mydir/../parse_ci_envs.sh"
if [ -e "${ci_envs}" ]; then
  # shellcheck disable=SC1091
  source "${ci_envs}"
fi

EXTERNAL_RPM_BUILD_OPTIONS=" --define \"scons_args ${SCONS_FAULTS_ARGS}\""
SCONS_ARGS="${SCONS_FAULTS_ARGS}"

: "${CHROOT_NAME:='epel-7-x86_64'}"
: "${TARGET:='centos7'}"

rm -rf "artifacts/${TARGET}/"
mkdir -p "artifacts/${TARGET}/"
DEBEMAIL="$DAOS_EMAIL" DEBFULLNAME="$DAOS_FULLNAME" \
TOPDIR=$PWD make CHROOT_NAME="${CHROOT_NAME}" \
    EXTERNAL_RPM_BUILD_OPTIONS="${EXTERNAL_RPM_BUILD_OPTIONS}" \
    SCONS_ARGS="${SCONS_ARGS}" -C utils/rpms chrootbuild
