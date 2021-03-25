#!/bin/bash

set -uex

# DAOS_PKG_VERSION environment variable needs to be set for this script

nodelist=(${NODELIST//,/ })
export PYTHONPATH="${PWD}/src/tests/ftest/util"
src/tests/ftest/config_file_gen.py -n "${nodelist[0]}" \
  -a /tmp/daos_agent.yml -s /tmp/daos_server.yml
src/tests/ftest/config_file_gen.py -n "${nodelist[0]}" -d /tmp/dmg.yml

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

scp -i ci_key /tmp/daos_agent.yml /tmp/dmg.yml /tmp/daos_server.yml \
              jenkins@"${nodelist[0]}":/tmp

# shellcheck disable=SC2029
ssh "$SSH_KEY_ARGS" jenkins@"${nodelist[0]}" \
  "NODE=${nodelist[0]}                       \
   DAOS_PKG_VERSION=$DAOS_PKG_VERSION        \
   $(cat "$mydir/test_daos_node.sh")"
