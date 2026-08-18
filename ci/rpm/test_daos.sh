#!/bin/bash

set -uex

# DAOS_PKG_VERSION environment variable needs to be set for this script
: "${PYTHON_VERSION:=3.11}"

# shellcheck disable=SC2153
IFS=" " read -r -a nodelist <<< "${NODELIST//,/ }"
mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# shellcheck disable=SC2029
ssh "$SSH_KEY_ARGS" jenkins@"${nodelist[0]}" \
  "bash -lc 'NODE=${nodelist[0]} DAOS_PKG_VERSION=$DAOS_PKG_VERSION PYTHON_VERSION=$PYTHON_VERSION \
   bash -s 2>&1 | tee /tmp/test_daos_rpms.log; exit \${PIPESTATUS[0]}'" \
  < "$mydir/test_daos_node.sh"
