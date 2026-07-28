#!/bin/bash
set -euo pipefail

DAOS_TARGET_TYPE="${DAOS_TARGET_TYPE:-release}"
DAOS_DEPS_JOBS="${DAOS_DEPS_JOBS:-32}"
echo "Building DAOS dependencies... with $DAOS_DEPS_JOBS jobs and target type \"$DAOS_TARGET_TYPE\""

export UV_NO_BUILD_ISOLATION=1
scons install --build-deps=only USE_INSTALLED=all PREFIX=/opt/daos \
      TARGET_TYPE="$DAOS_TARGET_TYPE" -j "$DAOS_DEPS_JOBS"
