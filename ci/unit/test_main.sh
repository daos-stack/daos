#!/bin/bash

# This is the script used for running unit testing
# run_tests.sh and run_tests.sh with memcheck stages on the CI
set -ex

# shellcheck disable=SC1091

# JENKINS-52781 tar function is breaking symlinks
source ./.build_vars.sh
rm -f "${SL_BUILD_DIR}/src/control/src/github.com/daos-stack/daos/src/control"
mkdir -p "${SL_BUILD_DIR}/src/control/src/github.com/daos-stack/daos/src/"
ln -s ../../../../../../../../src/control \
  "${SL_BUILD_DIR}/src/control/src/github.com/daos-stack/daos/src/control"
DAOS_BASE=${SL_PREFIX%/install*}
NODE=${NODELIST%%,*}
mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

rm -rf test_results
mkdir test_results
rm -f dnt.*.memcheck.xml nlt-errors.json

# shellcheck disable=SC2029
ssh "$SSH_KEY_ARGS" jenkins@"$NODE" "DAOS_BASE=$DAOS_BASE      \
                                     HOSTNAME=$HOSTNAME        \
                                     HOSTPWD=$PWD              \
                                     SL_PREFIX=$SL_PREFIX      \
                                     WITH_VALGRIND=$WITH_VALGRIND \
                                     $(cat "$mydir/test_main_node.sh")"
