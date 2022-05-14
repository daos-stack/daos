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
set -uex

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ci_envs="$mydir/../parse_ci_envs.sh"
if [ -e "${ci_envs}" ]; then
  # shellcheck disable=SC1091,SC1090
  source "${ci_envs}"
fi

: "${SCONS_FAULTS_ARGS:=BUILD_TYPE=dev}"
SCONS_ARGS="${SCONS_FAULTS_ARGS}"

: "${CHROOT_NAME:='centos+epel-7-x86_64'}"
: "${TARGET:='el8'}"

: "${COVFN_DISABLED:=true}"
if $COVFN_DISABLED; then
  JOB_REPOS=""
  EXTERNAL_COMPILER_OPT=""
else
  COV_REPO="${REPOSITORY_URL}repository/bullseye-el-7-x86_64/"
  JOB_REPOS="JOB_REPOS=${COV_REPO}"
  COMPILER_ARGS="COMPILER=covc"
  EXTERNAL_COMPILER_OPT=" --define \"compiler_args ${COMPILER_ARGS}\""
fi

EXTERNAL_SCONS_OPT=" --define \"scons_args ${SCONS_ARGS}\""
EXTERNAL_RPM_BUILD_OPTIONS="${EXTERNAL_SCONS_OPT}${EXTERNAL_COMPILER_OPT}"

rm -rf "artifacts/${TARGET}/"
mkdir -p "artifacts/${TARGET}/"

# shellcheck disable=SC2086
DEBEMAIL="$DAOS_EMAIL" DEBFULLNAME="$DAOS_FULLNAME"               \
TOPDIR=$PWD make CHROOT_NAME="${CHROOT_NAME}" ${JOB_REPOS}        \
    EXTERNAL_RPM_BUILD_OPTIONS="${EXTERNAL_RPM_BUILD_OPTIONS}"    \
    SCONS_ARGS="${SCONS_ARGS}" DISTRO_VERSION="${DISTRO_VERSION}" \
    -C utils/rpms chrootbuild
