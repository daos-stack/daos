#!/bin/bash

set -ex

# JENKINS-52781 tar function is breaking symlinks
rm -rf test_results
mkdir test_results
rm -f build/src/control/src/github.com/daos-stack/daos/src/control
mkdir -p build/src/control/src/github.com/daos-stack/daos/src/
ln -s ../../../../../../../../src/control \
      build/src/control/src/github.com/daos-stack/daos/src/control
# shellcheck disable=SC1091
. ./.build_vars.sh
DAOS_BASE=${SL_PREFIX%/install*}
NODE=${NODELIST%%,*}
# shellcheck disable=SC2029
ssh "$SSH_KEY_ARGS" jenkins@"$NODE" "DAOS_BASE=$DAOS_BASE      \
                                     HOSTNAME=$HOSTNAME        \
                                     PWD=$PWD                  \
                                     $(cat ci/run_test/core.sh)"
