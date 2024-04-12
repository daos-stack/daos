#!/bin/bash
# shellcheck disable=SC1113
# /*
#  * (C) Copyright 2016-2024 Intel Corporation.
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
# */

set -eux

# check that vm.max_map_count has been configured/bumped
if [ "$(sudo sysctl -n vm.max_map_count)" -lt "1000000" ] ; then
    echo "vm.max_map_count is not set as expected"
    exit 1
fi

# shellcheck disable=SC2153
mapfile -t TEST_TAG_ARR <<< "$TEST_TAG_ARG"

if  [ -d venv ]
then
    rm -rf venv
fi

python3 -m venv venv
# shellcheck disable=SC1091
source venv/bin/activate

pip install --upgrade pip
pip install -r "$PREFIX"/lib/daos/TESTING/ftest/requirements-ftest.txt

if $TEST_RPMS; then
    rm -rf "$PWD"/install/tmp
    mkdir -p "$PWD"/install/tmp
    # set the shared dir
    # TODO: remove the need for a shared dir by copying needed files to
    #       the test nodes
    export DAOS_TEST_SHARED_DIR=${DAOS_TEST_SHARED_DIR:-$PWD/install/tmp}
    logs_prefix="/var/tmp"
else
    rm -rf "$DAOS_BASE"/install/tmp
    mkdir -p "$DAOS_BASE"/install/tmp
    logs_prefix="$DAOS_BASE/install/lib/daos/TESTING"
    cd "$DAOS_BASE"
fi

# Copy the pydaos source locally and install it, in an ideal world this would install
# from the read-only tree directly but for now that isn't working.
# https://github.com/pypa/setuptools/issues/3237
cp -a "$PREFIX"/lib/daos/python pydaos
pip install ./pydaos
rm -rf pydaos

# Disable D_PROVIDER to allow launch.py to set it
unset D_PROVIDER

# Disable D_INTERFACE to allow launch.py to pick the fastest interface
unset D_INTERFACE

# At Oct2018 Longmond F2F it was decided that per-server logs are preferred
# But now we need to collect them!  Avoid using 'client_daos.log' due to
# conflicts with the daos_test log renaming.
# shellcheck disable=SC2153
export D_LOG_FILE="$TEST_TAG_DIR/daos.log"

pushd "$PREFIX"/lib/daos/TESTING/ftest

# make sure no lingering corefiles or junit files exist
rm -f core.* ./*_results.xml

# see if we just wanted to set up
if ${SETUP_ONLY:-false}; then
    exit 0
fi

# need to increase the number of oopen files (on EL8 at least)
ulimit -n 4096

# Clean stale job results
if [ -d "${logs_prefix}/ftest/avocado/job-results" ]; then
    rm -rf "${logs_prefix}/ftest/avocado/job-results"
fi

# now run it!
# shellcheck disable=SC2086
export WITH_VALGRIND
export STAGE_NAME
export TEST_RPMS
export DAOS_BASE
export DAOS_TEST_APP_SRC=${DAOS_TEST_APP_SRC:-"/scratch/daos_test/apps"}
export DAOS_TEST_APP_DIR=${DAOS_TEST_APP_DIR:-"${DAOS_TEST_SHARED_DIR}/daos_test/apps"}

launch_node_args="-ts ${TEST_NODES}"
if [ "${STAGE_NAME}" == "Functional Hardware 24" ]; then
    # Currently the 'Functional Hardware 24' uses a cluster that has 8 hosts configured to run
    # daos engines and the remaining hosts are configured to be clients. Use separate -ts and -tc
    # launch.py arguments to ensure these hosts are not used for unintended role
    IFS=" " read -r -a test_node_list <<< "${TEST_NODES//,/ }"
    server_nodes=$(IFS=','; echo "${test_node_list[*]:0:8}")
    client_nodes=$(IFS=','; echo "${test_node_list[*]:8}")
    launch_node_args="-ts ${server_nodes} -tc ${client_nodes}"
fi

# shellcheck disable=SC2086,SC2090,SC2048
if ! python ./launch.py --failfast --mode ci ${launch_node_args} ${LAUNCH_OPT_ARGS} ${TEST_TAG_ARR[*]}; then
    rc=${PIPESTATUS[0]}
else
    rc=0
fi

exit $rc
