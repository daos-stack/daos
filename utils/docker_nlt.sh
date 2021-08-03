#!/bin/bash

# Script for running NLT in a docker container.  This is called from Jenkinsfile
# where needed, and is a cheat way of running setup_daos_admin under sudo
# and NLT itself from a single script.

set -e

. utils/sl/setup_local.sh

./utils/setup_daos_admin.sh

# using mmap()'ed ULT stacks requires to bump system default
sysctl vm.max_map_count=1000000
./utils/node_local_test.py --no-root --memcheck no --server-debug WARN "$@"
