#!/bin/bash

# This is the script used for running utils/node_local_test.py (NLT)
set -uex

rm -rf nlt_logs

# Remove any logs from a previous run
rm -rf dnt.*.memcheck.xml vm_test/
NODE=${NODELIST%%,*}
mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# Copy over the install tree and some of the build tree. The memcheck NLT stage
# ships the valgrind-tagged build (opt-daos-valgrind.tar); the fault-injection
# stage ships the standard opt-daos.tar. Use whichever was unstashed.
opt_tar=opt-daos.tar
[ -f opt-daos-valgrind.tar ] && opt_tar=opt-daos-valgrind.tar
rm -f opt-daos-install.tar
ln "$opt_tar" opt-daos-install.tar
rsync -rlpt -z -e "ssh $SSH_KEY_ARGS" .build_vars* opt-daos-install.tar utils requirements-utest.txt jenkins@"$NODE":build/

ssh -T "$SSH_KEY_ARGS" jenkins@"$NODE" \
    "DAOS_HTTPS_PROXY=\"${DAOS_HTTPS_PROXY:-}\" \
     DAOS_NO_PROXY=\"${DAOS_NO_PROXY:-}\" \
     bash -s -- $*" < "$mydir/test_nlt_node.sh"
