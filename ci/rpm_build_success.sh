#!/bin/bash

# Script to be run on successful RPM build

set -uex

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ci_envs="$mydir/parse_ci_envs.sh"
if [ -e "${ci_envs}" ]; then
  # shellcheck disable=SC1091
  source "${ci_envs}"
fi

: "${CHROOT_NAME:='epel-7-x86_64'}"
: "${TARGET:='centos7'}"

mockroot="/var/lib/mock/${CHROOT_NAME}"
(cd "$mockroot/result/" && cp -r . "$OLDPWD/artifacts/${TARGET}"/)
createrepo "artifacts/${TARGET}/"
rpm --qf %{version}-%{release}.%{arch} \
    -qp artifacts/${TARGET}/daos-server-*.x86_64.rpm > "${TARGET}-rpm-version"
cat "$mockroot"/result/{root,build}.log
