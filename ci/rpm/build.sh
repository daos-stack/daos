#!/bin/bash

# Script for building RPMs in a chroot
# If the STAGE_NAME environment variable is present the script will
# attempt to parse it to determine what distribution to build for.
#
# For manual testing, you can set the environment variables CHROOT_NAME
# and TARGET can be set.
#
# Default is to build for CentOS 7.

set -ex

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ci_envs="$mydir/../parse_ci_envs.sh"
if [ -e "${ci_envs}" ]; then
  # shellcheck disable=SC1091
  source "${ci_envs}"
fi

: "${CHROOT_NAME:='epel-7-x86_64'}"
: "${TARGET:='centos7'}"

rm -rf "artifacts/${TARGET}/"
mkdir -p "artifacts/${TARGET}/"
DEBEMAIL="$DAOS_EMAIL" DEBFULLNAME="$DAOS_FULLNAME" \
TOPDIR=$PWD make CHROOT_NAME="${CHROOT_NAME}" -C utils/rpms chrootbuild
