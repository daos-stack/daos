#!/bin/bash

# Script for running NLT in a docker container.  This is called from Jenkinsfile
# where needed, and is a cheat way of running setup_daos_admin under sudo
# and NLT itself from a single script.

set -e

. utils/sl/setup_local.sh

sudo --preserve-env=SL_PREFIX ./utils/setup_daos_admin.sh

mkdir /tmp/n

cp utils/node_local_test.py utils/nlt_server.yaml utils/nlt_agent.yaml .build_vars.json src/tests/ftest/cart/util/cart_logparse.py src/tests/ftest/cart/util/cart_logtest.py /tmp/n

cd /tmp/n

./node_local_test.py --no-root --memcheck no --server-debug WARN "$@"
