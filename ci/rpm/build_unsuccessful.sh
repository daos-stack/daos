#!/bin/bash

# Script to be run on unsuccessful RPM build

set -uex

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ci_envs="$mydir/../parse_ci_envs.sh"
if [ -e "${ci_envs}" ]; then
  # at some point we want to use: shellcheck source=ci/parse_ci_envs.sh
  # shellcheck disable=SC1091
  source "${ci_envs}"
fi

: "${CHROOT_NAME:=epel-7-x86_64}"
: "${TARGET:=centos7}"

artdir="${PWD}/artifacts/${TARGET}"

if [ -d /var/cache/pbuilder/ ]; then
    mockroot=/var/cache/pbuilder/
    (if cd "$mockroot/result/"; then
      cp -r . "$artdir"
    fi)
    exit 0
fi

mockroot="/var/lib/mock/${CHROOT_NAME}"
cat "$mockroot"/result/{root,build}.log 2>/dev/null || true

if srpms="$(ls _topdir/SRPMS/*)"; then
  cp -af "$srpms" "$artdir"
fi
(if cd "$mockroot/result/"; then
  cp -r . "$artdir"
fi)

if ls "$mockroot"/root/builddir/build/BUILD/daos-*/config.log; then
    mv "$mockroot"/root/builddir/build/BUILD/daos-*/config.log \
        "${artdir}"/config.log-rpm
fi
