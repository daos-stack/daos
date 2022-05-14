#!/bin/bash

set -uex

rm -rf "_build.external${arch:-}"
mkdir -p coverity
rm -f coverity/*

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ci_envs="$mydir/parse_ci_envs.sh"
if [ -e "${ci_envs}" ]; then
  # shellcheck disable=SC1091
  source "${ci_envs}"
fi

: "${TARGET:='el8'}"

if [ -f "config${arch:-}.log" ]; then
  mv "config${arch}.log" "coverity/config.log-${TARGET}-cov"
fi
if [ -f cov-int/build-log.txt ]; then
  mv cov-int/build-log.txt coverity/cov-build-log.txt
fi
