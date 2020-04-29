#!/bin/bash

test_rpc1() {

    foo=bar
    echo "$foo"
}

test_rpc2() {
    local arg1="$1"

    foo="$arg1"
    echo "$foo"
}

test_rpc3() {
    local arg1="$1"
    local arg2="$2"

    foo="$arg1"
    echo "$foo"

    foo="$arg2"
    echo "$foo"
}

func_test_prep_1st_node() {
    set -ex
    local NODELIST="$1"

    systemctl start nfs-server.service
    mkdir -p /export/share
    chown jenkins /export/share
    echo "/export/share ${NODELIST//,/(rw,no_root_squash) }(rw,no_root_squash)" > /etc/exports
    exportfs -ra
}

func_test_prep_all_nodes() {
    set -ex
    local first_node="$1"

    for i in 0 1; do
        if [ -e /sys/class/net/ib$i ]; then
            if ! ifconfig ib$i | grep "inet "; then
              {
                echo "Found interface ib$i down after reboot on $HOSTNAME"
                systemctl status
                systemctl --failed
                journalctl -n 500
                ifconfig ib$i
                cat /sys/class/net/ib$i/mode
                ifup ib$i
              } | mail -s "Interface found down after reboot" "$OPERATIONS_EMAIL"
            fi
        fi
    done
    if ! grep /mnt/share /proc/mounts; then
        mkdir -p /mnt/share
        mount "$first_node":/export/share /mnt/share
    fi
}

func_test_run_rpms_ftest() {
    set -ex
    local test_tag="$1"
    local tnodes="$2"
    local ftest_arg="$3"

    local DAOS_TEST_SHARED_DIR=
    DAOS_TEST_SHARED_DIR=\\$(mktemp -d -p /mnt/share/)
    trap 'rm -rf $DAOS_TEST_SHARED_DIR' EXIT
    export DAOS_TEST_SHARED_DIR
    export TEST_RPMS=true
    export REMOTE_ACCT=jenkins
    /usr/lib/daos/TESTING/ftest/ftest.sh "$test_tag" "$tnodes" "$ftest_arg"
}

func_test_rpms_collect_logs() {
    set -ex
    tar -C /var/tmp/ -czf - ftest | tar -C install/lib/daos/TESTING/ -xzf -
}

run_test() {
    local DAOS_BASE="$1"
    local HOSTNAME="$2"
    local PWD="$3"

    set -ex

    sudo bash -c 'echo 1 > /proc/sys/kernel/sysrq'
    if grep /mnt/daos\  /proc/mounts; then
        sudo umount /mnt/daos
    else
        sudo mkdir -p /mnt/daos
    fi
    sudo mount -t tmpfs -o size=16G tmpfs /mnt/daos
    sudo mkdir -p $DAOS_BASE
    sudo mount -t nfs $HOSTNAME:$PWD $DAOS_BASE

    # copy daos_admin binary into $PATH and fix perms
    sudo cp $DAOS_BASE/install/bin/daos_admin /usr/bin/daos_admin && \
        sudo chown root /usr/bin/daos_admin && \
        sudo chmod 4755 /usr/bin/daos_admin && \
        mv $DAOS_BASE/install/bin/daos_admin \
           $DAOS_BASE/install/bin/orig_daos_admin

    # set CMOCKA envs here
    export CMOCKA_MESSAGE_OUTPUT=xml
    export CMOCKA_XML_FILE="$DAOS_BASE"/test_results/%g.xml
    cd $DAOS_BASE
    IS_CI=true OLD_CI=false utils/run_test.sh
}