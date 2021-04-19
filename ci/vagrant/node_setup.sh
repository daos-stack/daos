#!/bin/bash

set -eux

if ! ssh -i ci_key root@"$NODE" "NFS_SERVER=$NFS_SERVER \
                                 WORKDIR=$PWD           \
                                 $(cat ci/vagrant/node_setup_node.sh)"; then
    if [ "${PIPESTATUS[0]}" = "99" ]; then
        scp -r -i ci_key ci Vagrantfile jenkins@"$NODE":"$PWD"/
    fi
fi
