#!/bin/bash

set -uex

source /etc/os-release
: "${ID_LIKE:=unknown}"
: "${ID:=unknown}"
MAJOR_VERSION="${VERSION_ID%%.*}"
export MAJOR_VERSION
if [[ $ID_LIKE == *rhel* ]]; then
  PUBLIC_DISTRO=el
elif [[ $ID == *leap* ]]; then
  PUBLIC_DISTRO=leap
fi
export PUBLIC_DISTRO
