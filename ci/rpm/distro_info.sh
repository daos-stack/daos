#!/bin/bash
set -uex

# Script intended to be sourced to provide normalized names to allow
# other scripts to work for more than one distro.

# shellcheck disable=SC1091
source /etc/os-release
: "${ID_LIKE:=unknown}"
: "${ID:=unknown}"
MAJOR_VERSION="${VERSION_ID%%.*}"
export MAJOR_VERSION
if [[ $ID_LIKE == *rhel* ]]; then
  if [ "$MAJOR_VERSION" -eq 7 ]; then
    PUBLIC_DISTRO=centos
  else
    PUBLIC_DISTRO=el
  fi
elif [[ $ID == *leap* ]]; then
  PUBLIC_DISTRO=leap
fi
export PUBLIC_DISTRO
