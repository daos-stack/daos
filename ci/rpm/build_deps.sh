#!/bin/bash
#
#  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
#
# Build DAOS dependencies

set -uex

code_coverage="${1:-false}"
bullseye_key="${2:-}"

if [[ "${code_coverage}" == "true" ]] ; then
    utils/rpms/bullseye_build.sh "${bullseye_key}"
fi

scons install --build-deps=only USE_INSTALLED=all PREFIX=/opt/daos TARGET_TYPE=release -j 32
