#!/bin/bash

# Script to be run on unsuccessful RPM build

set -uex

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ci_envs="$mydir/parse_ci_envs.sh"
if [ -e "${ci_envs}" ]; then
  # shellcheck disable=SC0191
  source "${ci_envs}"
fi

: "${CHROOT_NAME:=epel-7-x86_64}"
: "${TARGET:=centos7}"

mockroot=/var/lib/mock/${CHROOT_NAME}
cat "$mockroot"/result/{root,build}.log 2>/dev/null || true

artdir="${PWD}/artifacts/${TARGET}"
if srpms="$(ls _topdir/SRPMS/*)"; then
  cp -af "$srpms" "$artdir"
fi
(if cd "$mockroot/result/"; then
  cp -r . "$artdir"
fi)
