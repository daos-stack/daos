#!/bin/bash

# This is a script to be run by the ci/unit/test_nlt.sh to run a test
# on a CI node.

set -uex

sudo bash -c 'echo 1 > /proc/sys/kernel/sysrq'
sudo mkdir -p /mnt/daos
# using mmap()'ed ULT stacks requires to bump system default
if [ "$(sudo sysctl -n vm.max_map_count)" -lt "1000000" ] ; then
    sudo sysctl vm.max_map_count=1000000
fi

cd build
tar -xf opt-daos.tar
sudo mv opt/daos /opt/

# Setup daos admin etc.
sudo bash -c ". ./utils/sl/setup_local.sh; ./utils/setup_daos_server_helper.sh"

# NLT will mount /mnt/daos itself.
# TODO: Enable this for DAOS-10905
# ./utils/node_local_test.py --max-log-size 1500MiB --dfuse-dir /localhome/jenkins/ \
#			   --server-valgrind all

./utils/node_local_test.py --max-log-size 1500MiB --dfuse-dir /localhome/jenkins/ \
    --log-usage-save nltir.xml --log-usage-export nltr.json all
