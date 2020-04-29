#!/bin/bash

# shellcheck source=ci/functions.sh
. ci/functions.sh

SSH_KEY_ARGS=%s
NODELIST=%s

# JENKINS-52781 tar function is breaking symlinks
rm -rf test_results
mkdir test_results
rm -f build/src/control/src/github.com/daos-stack/daos/src/control
mkdir -p build/src/control/src/github.com/daos-stack/daos/src/
ln -s ../../../../../../../../src/control build/src/control/src/github.com/daos-stack/daos/src/control
# shellcheck disable=SC1091
. ./.build_vars.sh
DAOS_BASE=${SL_PREFIX%/install*}
NODE=${NODELIST%%,*}
rpc "-o $SSH_KEY_ARGS -l jenkins" $NODE run_test "$DAOS_BASE" "$HOSTNAME" "$PWD"