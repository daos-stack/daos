#!/bin/bash

# This is a script to be run by the ci/unit/test_nlt.sh to run a test
# on a CI node.

set -uex

sudo bash -c 'echo 1 > /proc/sys/kernel/sysrq'
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

# Use the latest version that CI has available.
python3.11 -m venv venv
# shellcheck disable=SC1091
source venv/bin/activate

cat <<EOF > venv/pip.conf
[global]
    progress_bar = off
    no_color = true
    quiet = 1
EOF

pip install --upgrade pip

pip install --requirement requirements-utest.txt
pip install /opt/daos/lib/daos/python/

# set high open file limit in the shell to avoid extra warning
sudo prlimit --nofile=1024:262144 --pid $$
prlimit -n

mkdir -p nlt_logs
avail_line=$(grep '^MemAvailable:' /proc/meminfo)
avail_mem_kib=${avail_line//[^0-9]/}
if [ "$avail_mem_kib" -lt $((4 * 1024 * 1024)) ]; then
    echo "ERROR: Less than 4GiB RAM available for nlt_logs tmpfs (${avail_mem_kib} KiB)" >&2
    exit 1
fi
sudo mount -t tmpfs -o size=4g tmpfs nlt_logs
sudo chown jenkins:jenkins nlt_logs

exec env \
    TMPDIR="$(pwd)/nlt_logs" \
    HTTPS_PROXY="${DAOS_HTTPS_PROXY:-}" \
    NO_PROXY="${DAOS_NO_PROXY:-}" \
    ./utils/node_local_test.py "$@"
