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

log_prefix="unit_test"

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
    log_prefix="covc_test"
fi

cd "${SL_SRC_DIR}"
mkdir new_dir
sudo cp -a new_dir /opt/daos
tar --strip-components=2 --directory /opt/daos -xf opt-daos.tar

sudo bash -c ". ./utils/sl/setup_local.sh; ./utils/setup_daos_server_helper.sh"

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
RUN_TEST_VALGRIND=""
if [ "$WITH_VALGRIND" = "memcheck" ]; then
    log_prefix="unit_test_memcheck"
    RUN_TEST_VALGRIND="--memcheck"
fi
VDB_ARG=""
if [ -b "/dev/vdb" ]; then
    VDB_ARG="--bdev=/dev/vdb"
fi
SUDO_ARG="--sudo=no"
test_log_dir="${log_prefix}_logs"
if [ "$BDEV_TEST" = "true" ]; then
    SUDO_ARG="--sudo=only"
    test_log_dir="${log_prefix}_bdev_logs"
fi

rm -rf "$test_log_dir"

utils/run_utest.py $RUN_TEST_VALGRIND --no-fail-on-error $VDB_ARG --log_dir="$test_log_dir" \
                   $SUDO_ARG
