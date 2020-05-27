#!/bin/bash

# This is the script used for the run_tests.sh stage in CI
# for doing unit testing.
set -ex

# JENKINS-52781 tar function is breaking symlinks
rm -rf test_results
mkdir test_results
# shellcheck disable=SC1091
source ./.build_vars.sh
rm -f "${SL_BUILD_DIR}/src/control/src/github.com/daos-stack/daos/src/control"
mkdir -p "${SL_BUILD_DIR}/src/control/src/github.com/daos-stack/daos/src/"
ln -s ../../../../../../../../src/control \
  "${SL_BUILD_DIR}/src/control/src/github.com/daos-stack/daos/src/control"
DAOS_BASE=${SL_PREFIX%/install*}
rm -f dnt.*.memcheck.xml nlt-errors.json
NODE=${NODELIST%%,*}
mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# shellcheck disable=SC2029
ssh "$SSH_KEY_ARGS" jenkins@"$NODE" "DAOS_BASE=$DAOS_BASE      \
                                     HOSTNAME=$HOSTNAME        \
                                     HOSTPWD=$PWD              \
                                     SL_PREFIX=$SL_PREFIX      \
                                     $(cat "$mydir/run_test_main_node.sh")"
