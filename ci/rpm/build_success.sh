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
pwd
ls -la
artdir="${PWD}/artifacts/${TARGET}"
rpmdir="${PWD}/rpms"
rm -rf "$artdir"
mkdir -p "$artdir"
mkdir -p "$artdir/daos"

if [ -d "${rpmdir}" ]; then
  if [ -d "${rpmdir}/deps" ]; then
    mkdir -p "$artdir/deps"
    cp "${rpmdir}"/deps/*.rpm "${artdir}/deps"
  fi
  cp "${rpmdir}"/daos/*.rpm "${artdir}/daos"
fi

createrepo "$artdir"
