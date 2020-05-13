#!/bin/bash

set -uex

# DAOS_PKG_VERSION environment variable needs to be set for this script

if git show -s --format=%B | grep "^Skip-test: true"; then
  exit 0
fi
nodelist=("${NODELIST//,/ }")
src/tests/ftest/config_file_gen.py -n "${nodelist[0]}" \
  -a /tmp/daos_agent.yml -s /tmp/daos_server.yml
src/tests/ftest/config_file_gen.py -n "${nodelist[0]}" -d /tmp/dmg.yml
scp -i ci_key /tmp/daos_agent.yml jenkins@"${nodelist[0]}":/tmp
scp -i ci_key /tmp/dmg.yml jenkins@"${nodelist[0]}":/tmp
scp -i ci_key /tmp/daos_server.yml jenkins@"${nodelist[0]}":/tmp

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# shellcheck disable=SC2029
ssh "$SSH_KEY_ARGS" jenkins@"$NODE" "NODE=${nodelist[0]}  \
  DAOS_PKG_VERSION=$DAOS_PKG_VERSION                      \
  $(cat "$mydir/rpm_test_daos_test_node.sh")"
