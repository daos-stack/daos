# Test script
killall test_group_np_srv
sleep 1
set -x

export DAOS_PATH="/home/oganezov/github/daos/"

export D_PROVIDER="ucx+tcp"
export D_INTERFACE=enp3s0f0

export SRV_CMD=${DAOS_PATH}"./install/bin/crt_launch -e ${DAOS_PATH}install/lib/daos/TESTING/tests/test_group_np_srv --name fwd_test"

orterun --allow-run-as-root -np 2 -H hsw-204:15 -x D_LOG_MASK=info -x D_PROVIDER -x D_INTERFACE ${SRV_CMD} &

sleep 2

# Perform bulk forward transfers through rank=0 going to rank bulk_forward=1. size of bulk=2048, num repetitions=10
export CLI_CMD="${DAOS_PATH}./install/lib/daos/TESTING/tests/test_group_np_cli --name cl --attach_to fwd_test --rank=0 --bulk_forward=1 --skip_check_in --skip_shutdown -x 2048 -y 10"
${CLI_CMD}


