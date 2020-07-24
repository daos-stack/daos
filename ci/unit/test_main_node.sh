#!/bin/bash

# This is a script to be run by the unit/test_main.sh to run a test
# on a CI node.

set -ex

sudo bash -c 'echo 1 > /proc/sys/kernel/sysrq'
if grep /mnt/daos\  /proc/mounts; then
    sudo umount /mnt/daos
fi
sudo mkdir -p /mnt/daos

sudo mount -t tmpfs -o size=16G tmpfs /mnt/daos
sudo mkdir -p "$DAOS_BASE"
sudo mount -t nfs "$HOSTNAME":"$HOSTPWD" "$DAOS_BASE"
sudo cp "$DAOS_BASE/install/bin/daos_admin" /usr/bin/daos_admin
sudo chown root /usr/bin/daos_admin
sudo chmod 4755 /usr/bin/daos_admin
/bin/rm "$DAOS_BASE/install/bin/daos_admin"
sudo ln -sf "$SL_PREFIX/share/spdk/scripts/setup.sh" /usr/share/spdk/scripts
sudo ln -sf "$SL_PREFIX/share/spdk/scripts/common.sh" /usr/share/spdk/scripts
sudo ln -s "$SL_PREFIX/include"  /usr/share/spdk/include

cd "$DAOS_BASE"
if [ "$WITH_VALGRIND" = "memcheck" ]; then
    # run_test.sh with valgrind memcheck
    IS_CI=true OLD_CI=false RUN_TEST_VALGRIND=memcheck utils/run_test.sh
    ls
    ls test_results
    # Remove DAOS_BASE from memcheck xml results
    find test_results -maxdepth 1 \
        -name 'results-*-memcheck.xml' | xargs sed -i "s:$DAOS_BASE::g"
elif [ "$WITH_VALGRIND" = "disabled" ]; then
    # set CMOCKA envs here
    export CMOCKA_MESSAGE_OUTPUT=xml
    export CMOCKA_XML_FILE="$DAOS_BASE"/test_results/%g.xml
    IS_CI=true OLD_CI=false utils/run_test.sh
    ./utils/node_local_test.py all
fi
