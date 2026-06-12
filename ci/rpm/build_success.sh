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
  if [ -d /home/daos/rpms/deps ]; then
    mkdir -p "$artdir/deps"
    cp /home/daos/rpms/deps/*.rpm "${artdir}/deps"
  fi
  cp /home/daos/rpms/daos/*.rpm "${artdir}/daos"
fi

createrepo "$artdir"
