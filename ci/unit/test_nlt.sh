#!/bin/bash

# This is the script used for running utils/node_local_test.py (NLT)
set -uex

rm -rf nlt_logs

# Remove any logs from a previous run
rm -rf dnt.*.memcheck.xml vm_test/
NODE=${NODELIST%%,*}
mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# Copy over the install tree and some of the build tree.
rsync -rlpt -z -e "ssh $SSH_KEY_ARGS" .build_vars* opt-daos.tar utils jenkins@"$NODE":build/

# shellcheck disable=SC2029
ssh -tt "$SSH_KEY_ARGS" jenkins@"$NODE" "$(cat "$mydir/test_nlt_node.sh")"
