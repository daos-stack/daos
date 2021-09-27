#!/bin/bash

# Script for running NLT in a docker container.  This is called from Jenkinsfile
# where needed, and is a cheat way of running setup_daos_admin under sudo
# and NLT itself from a single script.

set -e

set -x

. utils/sl/setup_local.sh

sudo --preserve-env=SL_PREFIX ./utils/setup_daos_admin.sh

TMP_DIR=`mktemp`

cp utils/node_local_test.py utils/nlt_server.yaml utils/nlt_agent.yaml .build_vars.json src/tests/ftest/cart/util/cart_logparse.py src/tests/ftest/cart/util/cart_logtest.py $TMP_DIR

pushd $TMP_DIR

set +e

id
ls -l

./node_local_test.py --no-root --memcheck no --server-debug WARN "$@"

RC=$?
set -e
popd

cp $TMP_DIR/*.json .
cp $TMP_DIR/*.xml .

ls

exit $RC
