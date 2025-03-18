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

id
if [ "$(id -u)" = "0" ]; then
    echo "Should not be run as root"
    exit 1
fi

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ci_envs="$mydir/../parse_ci_envs.sh"
if [ -e "${ci_envs}" ]; then
  # shellcheck source=parse_ci_envs.sh disable=SC1091
  source "${ci_envs}"
fi

: "${SCONS_FAULTS_ARGS:=BUILD_TYPE=dev}"
SCONS_ARGS="${SCONS_FAULTS_ARGS}"

: "${CHROOT_NAME:='rocky+epel-8-x86_64'}"
: "${TARGET:='el8'}"
: "${REPO_SPEC:='el-8'}"

: "${COVFN_DISABLED:=true}"
: "${JOB_REPOS:=}"
EXTERNAL_COMPILER_OPT=""

if ! $COVFN_DISABLED && [[ $REPO_SPEC == el-* ]]; then
    compiler_args="COMPILER=covc"
    EXTERNAL_COMPILER_OPT=" --define \"compiler_args ${compiler_args}\""
fi

EXTERNAL_SCONS_OPT=" --define \"scons_args ${SCONS_ARGS}\""
EXTERNAL_RPM_BUILD_OPTIONS="${EXTERNAL_SCONS_OPT}${EXTERNAL_COMPILER_OPT}"

rm -rf "artifacts/${TARGET}/"
if ! mkdir -p "artifacts/${TARGET}/"; then
    echo "Failed to create directory \"artifacts/${TARGET}/\""
    ls -ld . || true
    pwd || true
    exit 1
fi

# shellcheck disable=SC2086
DEBEMAIL="$DAOS_EMAIL" DEBFULLNAME="$DAOS_FULLNAME"               \
TOPDIR=$PWD make CHROOT_NAME="${CHROOT_NAME}" ${JOB_REPOS}        \
    EXTERNAL_RPM_BUILD_OPTIONS="${EXTERNAL_RPM_BUILD_OPTIONS}"    \
    SCONS_ARGS="${SCONS_ARGS}" DISTRO_VERSION="${DISTRO_VERSION}" \
    -C utils/rpms chrootbuild
