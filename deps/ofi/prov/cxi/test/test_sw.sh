#!/bin/bash
#
# Run CXI unit tests for "hybrid" RX match mode.
# These tests are not supported by NETSIM.

DIR=`dirname $0`
cd $DIR
TEST_OUTPUT=cxitest.out

export DMA_FAULT_RATE=.1
export MALLOC_FAULT_RATE=.1
export FI_LOG_LEVEL=warn
export FI_LOG_PROV=cxi

# Run tests using hybrid RX mode, but do not constrain LE
#
#echo "running: FI_CXI_RX_MATCH_MODE=hybrid ./cxitest --verbose --tap=cxitest.tap -j2 > $TEST_OUTPUT 2>&1"
#FI_CXI_RX_MATCH_MODE=hybrid ./cxitest --verbose --tap=cxitest.tap -j2 > $TEST_OUTPUT 2>&1
#if [[ $? -ne 0 ]]; then
#    echo "cxitest return non-zero exit code. Possible failures in test teardown"
#    exit 1
#fi

# Run tests with constrained LE count - Using Flow Control recovery
MAX_ALLOC=`csrutil dump csr le_pools[63] |grep max_alloc |awk '{print $3}'`
csrutil store csr le_pools[] max_alloc=10 > /dev/null
echo "running;FI_CXI_RX_MATCH_MODE=hardware ./cxitest --verbose --filter=\"tagged/fc*\" --tap=cxitest-fc.tap -j1 > $TEST_OUTPUT 2>&1"
FI_CXI_RX_MATCH_MODE=hardware ./cxitest --verbose --filter="tagged/fc*" --tap=cxitest-fc.tap -j1 > $TEST_OUTPUT 2>&1
cxitest_exit_status=$?
csrutil store csr le_pools[] max_alloc=$MAX_ALLOC > /dev/null
if [[ $cxitest_exit_status -ne 0 ]]; then
    echo "cxitest return non-zero exit code. Possible failures in test teardown"
    exit 1
fi

# Run tests with constrained LE count - Using hybrid operation instead
# of flow control recovery
MAX_ALLOC=`csrutil dump csr le_pools[63] |grep max_alloc |awk '{print $3}'`
csrutil store csr le_pools[] max_alloc=10 > /dev/null
echo "running;FI_CXI_RX_MATCH_MODE=hybrid FI_CXI_RDZV_GET_MIN=0 ./cxitest --verbose --filter=\"tagged/fc*\" --tap=cxitest-sw-transition.tap -j1 >> $TEST_OUTPUT 2>&1"
FI_CXI_RX_MATCH_MODE=hybrid FI_CXI_RDZV_GET_MIN=0 ./cxitest --verbose --filter="tagged/fc*" --tap=cxitest-sw-transition.tap -j1 >> $TEST_OUTPUT 2>&1
cxitest_exit_status=$?
csrutil store csr le_pools[] max_alloc=$MAX_ALLOC > /dev/null
if [[ $cxitest_exit_status -ne 0 ]]; then
    echo "cxitest return non-zero exit code. Possible failures in test teardown"
    exit 1
fi

# Run HW to SW hybrid test with constrained LE count and forcing both
# eager and rendezvous processing
MAX_ALLOC=`csrutil dump csr le_pools[63] |grep max_alloc |awk '{print $3}'`
csrutil store csr le_pools[] max_alloc=60 > /dev/null
echo "running;FI_CXI_RX_MATCH_MODE=hybrid FI_CXI_RDZV_GET_MIN=0 FI_CXI_RDZV_THRESHOLD=2048 ./cxitest --verbose --filter=\"tagged/hw2sw_*\" --tap=cxitest-hw2sw-transition.tap -j1 >> $TEST_OUTPUT 2>&1"
FI_CXI_RX_MATCH_MODE=hybrid FI_CXI_RDZV_GET_MIN=0 FI_CXI_RDZV_THRESHOLD=2048 ./cxitest --verbose --filter="tagged/hw2sw_*" --tap=cxitest-hw2sw-transition.tap -j1 >> $TEST_OUTPUT 2>&1
csrutil store csr le_pools[] max_alloc=$MAX_ALLOC > /dev/null
if [[ $cxitest_exit_status -ne 0 ]]; then
    echo "cxitest return non-zero exit code. Possible failures in test teardown"
    exit 1
fi

# Run HW to SW hybrid test with constrained LE count and forcing only eager processing
MAX_ALLOC=`csrutil dump csr le_pools[63] |grep max_alloc |awk '{print $3}'`
echo "running;FI_CXI_RX_MATCH_MODE=hybrid FI_CXI_RDZV_GET_MIN=0 FI_CXI_RDZV_THRESHOLD=16384 ./cxitest --verbose --filter=\"tagged/hw2sw_*\" --tap=cxitest-hw2sw-eager-transition.tap -j1 >> $TEST_OUTPUT 2>&1"
FI_CXI_RX_MATCH_MODE=hybrid FI_CXI_RDZV_GET_MIN=0 FI_CXI_RDZV_THRESHOLD=16384 ./cxitest --verbose --filter="tagged/hw2sw_*" --tap=cxitest-hw2sw-transition.tap -j1 >> $TEST_OUTPUT 2>&1
cxitest_exit_status=$?
csrutil store csr le_pools[] max_alloc=$MAX_ALLOC > /dev/null
if [[ $cxitest_exit_status -ne 0 ]]; then
    echo "cxitest return non-zero exit code. Possible failures in test teardown"
    exit 1
fi

# Test scaling of request buffers
echo "running; FI_CXI_RX_MATCH_MODE=software FI_CXI_REQ_BUF_MIN_POSTED=2 FI_CXI_REQ_BUF_MAX_COUNT=10 ./cxitest --verbose --filter=\"tagged/*fc_mt\" --tap=cxitest-sw-reqbuf.tap -j1 >> $TEST_OUTPUT 2>&1"
FI_CXI_RX_MATCH_MODE=software FI_CXI_REQ_BUF_MIN_POSTED=2 FI_CXI_REQ_BUF_MAX_COUNT=10 ./cxitest --verbose --filter="tagged/*fc_mt" --tap=cxitest-sw-req_buf.tap -j1 >> $TEST_OUTPUT 2>&1
cxitest_exit_status=$?
if [[ $cxitest_exit_status -ne 0 ]]; then
    echo "cxitest return non-zero exit code. Possible failures in test teardown"
    exit 1
fi

grep "Tested" $TEST_OUTPUT
