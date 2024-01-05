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
# ./utils/node_local_test.py --max-log-size ????MiB --dfuse-dir /localhome/jenkins/ \
#			   --server-valgrind all

python3 -m venv venv
# shellcheck disable=SC1091
source venv/bin/activate
touch venv/pip.conf
pip config set global.progress_bar off
pip config set global.no_color true

pip install --upgrade pip
pip install --requirement requirements-utest.txt

cd src/client

pip install .

cd -

pip list

./utils/node_local_test.py --max-log-size 1700MiB --dfuse-dir /localhome/jenkins/ \
    --log-usage-save nltir.xml --log-usage-export nltr.json all
