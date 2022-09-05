#!/bin/bash

# This is a script to be run by the ci/unit/test_main.sh to run a test
# on a CI node.

set -uex

sudo bash -c 'echo 1 > /proc/sys/kernel/sysrq'
sudo bash -c 'echo 1024 > /proc/sys/vm/nr_hugepages'

if grep /mnt/daos\  /proc/mounts; then
    sudo umount /mnt/daos
fi
sudo mkdir -p /mnt/daos

sudo mkdir -p "$DAOS_BASE"
sudo mount -t nfs "$HOSTNAME":"$HOSTPWD" "$DAOS_BASE"
if [ -n "$BULLSEYE" ]; then
    pushd "$DAOS_BASE/bullseye"
    set +x
    echo + sudo ./install --quiet --key "**********" --prefix /opt/BullseyeCoverage
    sudo ./install --quiet --key "${BULLSEYE}" --prefix /opt/BullseyeCoverage
    set -x
    popd
    rm -rf bullseye
    export COVFILE="$DAOS_BASE/test.cov"
    export PATH="/opt/BullseyeCoverage/bin:$PATH"
fi

cd "$DAOS_BASE"
ls
tar -xf opt-daos.tar
sudo mv opt/daos /opt/

ls /opt/
find /opt/

sudo bash -c ". ./utils/sl/setup_local.sh; ./utils/setup_daos_admin.sh"
/bin/rm "$DAOS_BASE/install/bin/daos_admin"

sudo mkdir -p /usr/share/spdk/scripts/
sudo ln -sf "$SL_PREFIX/share/spdk/scripts/setup.sh" /usr/share/spdk/scripts/
sudo ln -sf "$SL_PREFIX/share/spdk/scripts/common.sh" /usr/share/spdk/scripts/
sudo ln -s "$SL_PREFIX/include"  /usr/share/spdk/include

# set CMOCKA envs here
export CMOCKA_MESSAGE_OUTPUT=xml
if [[ -z ${WITH_VALGRIND} ]]; then
    export CMOCKA_XML_FILE="${DAOS_BASE}/test_results/%g.xml"
else
    export CMOCKA_XML_FILE="${DAOS_BASE}/test_results/%g_${WITH_VALGRIND}.xml"
fi

sudo mount -t tmpfs -o size=16G tmpfs /mnt/daos
IS_CI=true OLD_CI=false RUN_TEST_VALGRIND="$WITH_VALGRIND" \
    DAOS_BASE="$DAOS_BASE" utils/run_test.sh
