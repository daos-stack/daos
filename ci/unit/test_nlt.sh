#!/bin/bash

# This is the script used for running utils/node_local_test.py (NLT)
set -uex

rm -rf nlt_logs

# shellcheck disable=SC1091
source ./.build_vars.sh
rm -f "${SL_BUILD_DIR}/src/control/src/github.com/daos-stack/daos/src/control"
mkdir -p "${SL_BUILD_DIR}/src/control/src/github.com/daos-stack/daos/src/"
ln -s ../../../../../../../../src/control \
  "${SL_BUILD_DIR}/src/control/src/github.com/daos-stack/daos/src/control"
DAOS_BASE=${SL_PREFIX%/install*}
# Remove any logs from a previous run
rm -rf dnt.*.memcheck.xml vm_test/
NODE=${NODELIST%%,*}
mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# Copy over the install tree and some of the build tree.
rsync -v -rlpt -z -e "ssh $SSH_KEY_ARGS" .build_vars* install utils \
      jenkins@"$NODE":build/

# shellcheck disable=SC2029
ssh -tt "$SSH_KEY_ARGS" jenkins@"$NODE" "DAOS_BASE=$DAOS_BASE      \
                                         HOSTNAME=$HOSTNAME        \
                                         HOSTPWD=$PWD              \
                                         SL_PREFIX=$SL_PREFIX      \
                                         $(cat "$mydir/test_nlt_node.sh")"
