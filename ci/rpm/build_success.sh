#!/bin/bash

# Script to be run on successful RPM build

set -uex

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ci_envs="$mydir/../parse_ci_envs.sh"
if [ -e "${ci_envs}" ]; then
  # shellcheck source=parse_ci_envs.sh
  source "${ci_envs}"
fi

: "${TARGET:=centos9}"

artdir="${PWD}/artifacts/${TARGET}"
rm -rf "$artdir"
mkdir -p "$artdir"
mkdir -p "$artdir/daos"

if [ -d /home/daos/rpms/ ]; then
  # shellcheck disable=SC2044
  for dir in $(find /home/daos/rpms/ -maxdepth 1 -mindepth 1 -type d -exec basename {} \;); do
    if [ -d "/home/daos/rpms/${dir}" ]; then
      mkdir -p "${artdir}/${dir}"
      cp "/home/daos/rpms/${dir}"/*.rpm "${artdir}/${dir}"
    fi
  done
fi

createrepo "$artdir"
