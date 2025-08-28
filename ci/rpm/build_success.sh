#!/bin/bash

# Script to be run on successful RPM build

set -uex

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ci_envs="$mydir/../parse_ci_envs.sh"
if [ -e "${ci_envs}" ]; then
  # shellcheck source=parse_ci_envs.sh
  source "${ci_envs}"
fi

: "${CHROOT_NAME:=epel-7-x86_64}"
: "${TARGET:=centos7}"

artdir="${PWD}/artifacts/${TARGET}"
rm -rf "$artdir"
mkdir -p "$artdir"
mkdir -p "$artdir/daos"

<<<<<<< HEAD
# ubuntu isn't complete since we don't have ubuntu stages
=======
>>>>>>> jvolivie/reintroduce_rpms
if [ -d /home/daos/pkgs/ ]; then
    cp /home/daos/pkgs/*.deb "${artdir}"
    pushd "$artdir"
    dpkg-scanpackages . /dev/null | \
        gzip -9c > Packages.gz
    popd

    exit 0
elif [ -d /var/cache/pbuilder/ ]; then
    mockroot=/var/cache/pbuilder/
    (if cd "$mockroot/result/"; then
      cp ./*{.buildinfo,.changes,.deb,.dsc,.xz} "$artdir"
    fi)
    cp utils/rpms/_topdir/BUILD/*.orig.tar.*  "$artdir"
    pushd "$artdir"
    dpkg-scanpackages . /dev/null | \
        gzip -9c > Packages.gz
    popd

    exit 0
fi

if [ -d /home/daos/rpms/ ]; then
<<<<<<< HEAD
  if [ -d /home/daos/rpms/deps ]; then
    mkdir -p "$artdir/deps"
    cp /home/daos/rpms/deps/*.rpm "${artdir}/deps"
  fi
  cp /home/daos/rpms/daos/*.rpm "${artdir}/daos"
=======
  cp /home/daos/rpms/*.rpm "${artdir}"
>>>>>>> jvolivie/reintroduce_rpms
else
  mockroot="/var/lib/mock/${CHROOT_NAME}"
  cat "$mockroot"/result/{root,build}.log 2>/dev/null || true

  if srpms="$(ls _topdir/SRPMS/*)"; then
    cp -af "$srpms" "$artdir"
  fi
  (if cd "$mockroot/result/"; then
    cp -r . "$artdir"
  fi)
fi

createrepo "$artdir"
