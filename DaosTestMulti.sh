#!/bin/bash

set -ex

if [ "$JENKINS_URL" = "http://localhost:8080/" ]; then
    NFS_SERVER="192.168.121.1"
else
    HOSTPREFIX="wolf-53"
fi
NFS_SERVER=${NFS_SERVER:-$HOSTPREFIX}

# leave this empty to run on the centos7 builder
CLIENT_VM=${HOSTPREFIX}vm1

# shellcheck disable=SC1091
. .build_vars.sh

daospath=${SL_OMPI_PREFIX%/install}
CPATH="${daospath}"/install/include/:"$CPATH"
PATH="${daospath}"/install/bin/:"${daospath}"/install/sbin:"$PATH"
LD_LIBRARY_PATH="${daospath}"/install/lib:"${daospath}"/install/lib/daos_srv:$LD_LIBRARY_PATH
export CPATH PATH LD_LIBRARY_PATH
export CRT_PHY_ADDR_STR="ofi+sockets"

cat <<EOF > hostfile
${HOSTPREFIX}vm1 slots=1
${HOSTPREFIX}vm2 slots=1
${HOSTPREFIX}vm3 slots=1
${HOSTPREFIX}vm4 slots=1
${HOSTPREFIX}vm5 slots=1
${HOSTPREFIX}vm6 slots=1
${HOSTPREFIX}vm7 slots=1
${HOSTPREFIX}vm8 slots=1
EOF

# shellcheck disable=SC1091
#. scons_local/utils/setup_local.sh

export SERVER_OFI_INTERFACE=eth0
if [ -n "$CLIENT_VM" ]; then
    export CLIENT_OFI_INTERFACE=eth0
else
    export CLIENT_OFI_INTERFACE=virbr1
fi

pdsh -R ssh -S -w ${HOSTPREFIX}vm[1-8] "sudo bash -c 'echo \"1\" > /proc/sys/kernel/sysrq'
if grep /mnt/daos\\  /proc/mounts; then
    sudo umount /mnt/daos
else
    if [ ! -d /mnt/daos ]; then
        sudo mkdir -p /mnt/daos
    fi
fi
sudo mkdir -p $daospath
sudo mount -t nfs $NFS_SERVER:$PWD $daospath
sudo mount -t tmpfs -o size=16G tmpfs /mnt/daos
df -h /mnt/daos" 2>&1 | dshbak -c

TESTS=${1:-"-mpceiACoRO"}

rm -f "${daospath}"/daos.log

# shellcheck disable=SC2029
ssh "$CLIENT_VM" "export LD_LIBRARY_PATH=$LD_LIBRARY_PATH
\"${daospath}\"/install/bin/orterun --np 8 --hostfile \"$daospath\"/hostfile --enable-recovery --report-uri \"$daospath\"/urifile  -x LD_LIBRARY_PATH=\"$LD_LIBRARY_PATH\" -x CRT_PHY_ADDR_STR=\"$CRT_PHY_ADDR_STR\" -x D_LOG_FILE=\"$daospath\"/daos.log -x ABT_ENV_MAX_NUM_XSTREAMS=100 -x ABT_MAX_NUM_XSTREAMS=100 -x PATH=\"$PATH\" -x OFI_PORT=23350 -x OFI_INTERFACE=\"$SERVER_OFI_INTERFACE\" \"${daospath}\"/install/bin/daos_server -g daos_server -c 1 >/tmp/daos_server.out 2>&1 &
daos_server_pid=\$!
if ! kill -0 \$daos_server_pid 2>/dev/null; then
    wait \$daos_server_pid
    exit \${PIPESTATUS[0]}
else
    echo \"\$daos_server_pid\" > /tmp/daos_server_pid
fi"
# shellcheck disable=SC2154
trap 'ssh "$CLIENT_VM" "daos_server_pid=\$(cat /tmp/daos_server_pid)
if kill -0 \$daos_server_pid 2>/dev/null; then
    kill \$daos_server_pid
fi"
pdsh -R ssh -S -w ${HOSTPREFIX}vm[1-8] "x=0
while [ \$x -lt 30 ] && grep $daospath /proc/mounts && ! sudo umount $daospath; do
    sleep 1
    let x=\$x+1
done
sudo rmdir $daospath" 2>&1 | dshbak -c
ls -l "$daospath"/daos.log' EXIT

sleep 5

# shellcheck disable=SC2029
if ! ssh "$CLIENT_VM" "daos_server_pid=\$(cat /tmp/daos_server_pid)
    if ! kill -0 \$daos_server_pid 2>/dev/null; then
        exit 199
    fi
    # cmocka's XML results are not JUnit compliant as it creates multiple
    # <testsuites> blocks and only one is allowed
    trap 'set -x; cat \"${daospath}\"/results.xml; sed -i -e '\"'\"'2!{/<testsuites>/d;}'\"'\"' -e '\"'\"'\$!{/<\\/testsuites>/d;}'\"'\"' \"${daospath}\"/results.xml; cat \"${daospath}\"/results.xml;' EXIT
    rm -f \"${daospath}\"/results.xml
    CMOCKA_XML_FILE=\"${daospath}\"/results.xml CMOCKA_MESSAGE_OUTPUT=xml \"${daospath}\"/install/bin/orterun --output-filename \"$daospath\"/daos_test.out --np 1 --ompi-server file:\"$daospath\"/urifile -x ABT_ENV_MAX_NUM_XSTREAMS=100 -x PATH=\"$PATH\" -x CRT_PHY_ADDR_STR=\"$CRT_PHY_ADDR_STR\" -x ABT_MAX_NUM_XSTREAMS=100 -x LD_LIBRARY_PATH=\"$LD_LIBRARY_PATH\" -x D_LOG_FILE=\"$daospath\"/daos.log -x OFI_INTERFACE=\"$CLIENT_OFI_INTERFACE\" daos_test \"$TESTS\""; then
    if [ "${PIPESTATUS[0]}" = 199 ]; then
        echo "daos_server not running"
        pdsh -R ssh -S -w ${HOSTPREFIX}vm[1-8] "if [ -f /tmp/daos_server.out ]; then cat /tmp/daos_server.out; fi" | dshbak -c
        trap 'pdsh -R ssh -S -w ${HOSTPREFIX}vm[1-8] "x=0
while [ \$x -lt 30 ] && grep $daospath /proc/mounts && ! sudo umount $daospath; do
    sleep 1
    let x=\$x+1
done
sudo rmdir $daospath" 2>&1 | dshbak -c' EXIT
        exit 199
    else
        echo "Running daos_test failed"
        exit 1
    fi
fi
