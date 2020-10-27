#!/bin/bash

# This is a script to be run by the unit/test_main.sh to run a test
# on a CI node.

set -uex

sudo bash -c 'echo 1 > /proc/sys/kernel/sysrq'
if grep /mnt/daos\  /proc/mounts; then
    sudo umount /mnt/daos
fi
sudo mkdir -p /mnt/daos

sudo mount -t tmpfs -o size=16G tmpfs /mnt/daos
sudo mkdir -p "$DAOS_BASE"
sudo mount -t nfs "$HOSTNAME":"$HOSTPWD" "$DAOS_BASE"
sudo cp "$DAOS_BASE/install/bin/daos_admin" /usr/bin/daos_admin
set +x
if [ -n "$BULLSEYE" ]; then
  pushd "$DAOS_BASE/bullseye"
    sudo ./install --quiet --key "${BULLSEYE}" \
                   --prefix /opt/BullseyeCoverage
  popd
  rm -rf bullseye
  export COVFILE="$DAOS_BASE/test.cov"
  export PATH="/opt/BullseyeCoverage/bin:$PATH"
fi
sudo chown root /usr/bin/daos_admin
sudo chmod 4755 /usr/bin/daos_admin
/bin/rm "$DAOS_BASE/install/bin/daos_admin"
sudo ln -sf "$SL_PREFIX/share/spdk/scripts/setup.sh" /usr/share/spdk/scripts
sudo ln -sf "$SL_PREFIX/share/spdk/scripts/common.sh" /usr/share/spdk/scripts
sudo ln -s "$SL_PREFIX/include"  /usr/share/spdk/include

# set CMOCKA envs here
export CMOCKA_MESSAGE_OUTPUT=xml
if [[ -z ${WITH_VALGRIND} ]]; then
    export CMOCKA_XML_FILE="${DAOS_BASE}/test_results/%g.xml"
else
    export CMOCKA_XML_FILE="${DAOS_BASE}/test_results/%g_${WITH_VALGRIND}.xml"
fi

cd "$DAOS_BASE"
IS_CI=true OLD_CI=false RUN_TEST_VALGRIND="$WITH_VALGRIND" utils/run_test.sh
mkdir -p vm_test

# Remove DAOS_BASE from memcheck xml results
set -x
if [ "$WITH_VALGRIND" == 'memcheck' ]; then
    find test_results -maxdepth 1 -name '*.memcheck.xml' \
        -print0 | xargs -0 sed -i "s:$DAOS_BASE::g"
    mv test_results/unit-test-*.memcheck.xml .
elif [ -z "$BULLSEYE" ]; then
    ./utils/node_local_test.py --output-file=vm_test/nlt-errors.json all
fi
