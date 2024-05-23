#!/bin/bash

set -uex

# DAOS_PKG_VERSION environment variable needs to be set for this script

# shellcheck disable=SC2153
IFS=" " read -r -a nodelist <<< "${NODELIST//,/ }"
mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# shellcheck disable=SC2029
ssh "$SSH_KEY_ARGS" jenkins@"${nodelist[0]}" \
  "NODE=${nodelist[0]}                       \
   DAOS_PKG_VERSION=$DAOS_PKG_VERSION        \
   $(cat "$mydir/test_daos_node.sh")"
