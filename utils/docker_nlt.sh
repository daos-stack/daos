#!/bin/bash

# Script for running NLT in a docker container.  This is called from Jenkinsfile
# where needed, and is a cheat way of running setup_daos_admin under sudo
# and NLT itself from a single script.

set -e

set -x

. utils/sl/setup_local.sh

sudo --preserve-env=SL_PREFIX,SL_SPDK_PREFIX ./utils/setup_daos_admin.sh

TMP_DIR=$(mktemp -d)

cp utils/node_local_test.py utils/nlt_server.yaml utils/nlt_agent.yaml .build_vars.json "$TMP_DIR"
cp src/tests/ftest/cart/util/cart_logparse.py src/tests/ftest/cart/util/cart_logtest.py "$TMP_DIR"

pushd "$TMP_DIR"

set +e

sudo ./node_local_test.py --no-root --memcheck no --server-debug WARN "$@"

RC=$?
set -e
popd

cp "$TMP_DIR"/*.json .
cp "$TMP_DIR"/*.xml .
sudo chmod -R o+r "$TMP_DIR"/nlt_logs
cp -r "$TMP_DIR"/nlt_logs .

exit $RC
