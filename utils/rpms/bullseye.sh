#!/bin/bash
root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
. "${root}/fpm_common.sh"

if [ -z "${SL_BULLSEYE_PREFIX:-}" ]; then
  echo "bullseye must be installed or never built in ${SL_BULLSEYE_PREFIX:-}"
  exit 0
fi

VERSION="${bullseye_version}"
RELEASE="${bullseye_release}"
LICENSE="Proprietary"
DESCRIPTION="The BullseyeCoverage compiler"
URL="https://www.bullseye.com/index.html"
RPM_CHANGELOG="bullseye.changelog"

PACKAGE_TYPE="dir"
files=()
list_files files "/opt/BullseyeCoverage/*"
append_install_list "${files[@]}"
build_package "bullseye"
