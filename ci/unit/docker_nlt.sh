#!/bin/sh

# Script for running NLT in a docker container.  This is called from Jenkinsfile
# where needed, and is a cheat way of running setup_daos_admin under sudo
# and NLT itself from a single script.

set -e

# Allow running in daos tree, or in parent.  This mostly allows local scripts to
# run, for example
# docker build . -f utils/docker/Dockerfile.centos.7 --build-arg DAOS_KEEP_SRC=yes -t mycont
# docker run -ti --tmpfs /mnt/daos mycont sudo ./ci/unit/docker_nlt.sh
# 
[ -d daos ] && cd daos

. utils/sl/setup_local.sh

./utils/setup_daos_admin.sh

./utils/node_local_test.py --no-root kv
