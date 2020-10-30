#!/bin/bash

# Script to be run on successful RPM build

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
      cp ./*{.buildinfo,.changes,.deb,.dsc,.xz} "$artdir"
    fi)
    cp utils/rpms/_topdir/BUILD/*.orig.tar.*  "$artdir"
    pushd "$artdir"
    dpkg-scanpackages . /dev/null | \
        gzip -9c > Packages.gz
    popd

    dpkg -f "$artdir"/daos-server_*_amd64.deb Version > "${TARGET}-rpm-version"
    ls -l "${TARGET}-rpm-version" || true
    cat "${TARGET}-rpm-version" || true
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

createrepo "$artdir"
rpm --qf "%{version}-%{release}.%{arch}" \
    -qp "$artdir"/daos-server-*.x86_64.rpm > "${TARGET}-rpm-version"
cat "$mockroot"/result/{root,build}.log
