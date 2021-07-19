#!/bin/sh
set -x

#daospath=$1
#nodes=$2
IFS=', ' read -r -a nodes_array <<< "$nodes"
for i in "${nodes_array[@]:1}"; do
    total_nodes+="${i}:5,"
done
total_nodes=${total_nodes%?}
echo $total_nodes

module avail
module load mpi
module list
orterun=$(which orterun)

#/usr/mpi/gcc/openmpi-4.1.0rc5/bin/orterun \
${orterun} \
    --mca btl self,tcp \
    -N 1 \
    -x D_LOG_FILE=/var/tmp/daos_testing/self_test_np_sep_cart_server.log \
    -x D_LOG_FILE_APPEND_PID=1 \
    -x PATH \
    -x D_LOG_MASK=WARN \
    -x CRT_PHY_ADDR_STR=ofi+sockets \
    -x CRT_CTX_SHARE_ADDR=0 \
    -x CRT_ATTACH_INFO_PATH=/home/standan/tmp/ \
    -x CRT_CTX_NUM=16 \
        -x OFI_INTERFACE=ib0 \
        --host "${total_nodes}" \
    crt_launch -e ${daospath}/lib/daos/TESTING/tests/test_group_np_srv --name selftest_srv_grp : \
        -x OFI_INTERFACE=ib1 \
        --host "${total_nodes}" \
    crt_launch -e ${daospath}/lib/daos/TESTING/tests/test_group_np_srv --name selftest_srv_grp&

sleep 5

#/usr/mpi/gcc/openmpi-4.1.0rc5/bin/orterun \
${orterun} \
    --mca btl self,tcp \
    -N 1 \
    --host ${nodes_array[1]} \
    -x D_LOG_FILE_APPEND_PID=1 \
    -x D_LOG_FILE=/var/tmp/daos_testing/self_test_np_sep_cart_client.log \
    -x PATH \
    -x D_LOG_MASK=WARN \
    -x CRT_PHY_ADDR_STR=ofi+sockets \
    -x OFI_INTERFACE=ib0 \
    -x CRT_CTX_SHARE_ADDR=0 \
    -x CRT_ATTACH_INFO_PATH=/home/standan/tmp/ \
    -x CRT_CTX_NUM=16 \
    self_test --group-name selftest_srv_grp \
        --endpoint 0-3:0 \
        --message-sizes "b2000,b2000 0,0 b2000,b2000 i1000,i1000
        b2000,i1000,i1000 0,0 i1000,1,0" \
        --max-inflight-rpcs 16 \
        --repetitions 1000 \
        -t \
        -n

killall -9 test_group_np_srv
killall -9 crt_launch
killall -9 self_test
killall -9 orterun
