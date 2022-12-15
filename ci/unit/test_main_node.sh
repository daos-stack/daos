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

source build/.build_vars.sh

sudo mkdir -p "${SL_SRC_DIR}"
sudo mount --bind build "${SL_SRC_DIR}"

if [ -n "$BULLSEYE" ]; then
    pushd "${SL_SRC_DIR}/bullseye"
    set +x
    echo + sudo ./install --quiet --key "**********" --prefix /opt/BullseyeCoverage
    sudo ./install --quiet --key "${BULLSEYE}" --prefix /opt/BullseyeCoverage
    set -x
    popd
    rm -rf bullseye
    export COVFILE="${SL_SRC_DIR}/test.cov"
    export PATH="/opt/BullseyeCoverage/bin:$PATH"
fi

cd "${SL_SRC_DIR}"
mkdir new_dir
sudo cp -a new_dir /opt/daos
tar --strip-components=2 --directory /opt/daos -xf opt-daos.tar

sudo bash -c ". ./utils/sl/setup_local.sh; ./utils/setup_daos_admin.sh"

sudo mkdir -p /usr/share/spdk/scripts/
sudo ln -sf "$SL_PREFIX/share/spdk/scripts/setup.sh" /usr/share/spdk/scripts/
sudo ln -sf "$SL_PREFIX/share/spdk/scripts/common.sh" /usr/share/spdk/scripts/
sudo ln -s "$SL_PREFIX/include"  /usr/share/spdk/include

# set CMOCKA envs here
export CMOCKA_MESSAGE_OUTPUT=xml
if [[ -z ${WITH_VALGRIND} ]]; then
    export CMOCKA_XML_FILE="${SL_SRC_DIR}/test_results/%g.xml"
else
    export CMOCKA_XML_FILE="${SL_SRC_DIR}/test_results/%g_${WITH_VALGRIND}.xml"
fi

sudo mount -t tmpfs -o size=16G tmpfs /mnt/daos
IS_CI=true RUN_TEST_VALGRIND="$WITH_VALGRIND" \
    DAOS_BASE="$SL_SRC_DIR" utils/run_test.sh
