#!/bin/bash
# Copyright 2020-2022 Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted for any purpose (including commercial purposes)
# provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions, and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions, and the following disclaimer in the
#    documentation and/or materials provided with the distribution.
#
# 3. In addition, redistributions of modified forms of the source or binary
#    code must carry prominent notices stating that the original code was
#    changed and the date of the change.
#
#  4. All publications or advertising materials mentioning features or use of
#     this software are asked, but not required, to acknowledge that it was
#     developed by Intel Corporation and credit the contributors.
#
# 5. Neither the name of Intel Corporation, nor the name of any Contributor
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set -eux

# check that vm.max_map_count has been configured/bumped
if [ "$(sudo sysctl -n vm.max_map_count)" -lt "1000000" ] ; then
    echo "vm.max_map_count is not set as expected"
    exit 1
fi

# shellcheck disable=SC2153
mapfile -t TEST_TAG_ARR <<< "$TEST_TAG_ARG"

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

# Disable CRT_PHY_ADDR_STR to allow launch.py to set it
unset CRT_PHY_ADDR_STR

# Disable OFI_INTERFACE to allow launch.py to pick the fastest interface
unset OFI_INTERFACE

# At Oct2018 Longmond F2F it was decided that per-server logs are preferred
# But now we need to collect them!  Avoid using 'client_daos.log' due to
# conflicts with the daos_test log renaming.
# shellcheck disable=SC2153
export D_LOG_FILE="$TEST_TAG_DIR/daos.log"

# Give the avocado test tearDown method a minimum of 120 seconds to complete when the test process
# has timed out.  The test harness will increment this timeout based upon the number of pools
# created in the test to account for pool destroy command timeouts.
mkdir -p ~/.config/avocado/
cat <<EOF > ~/.config/avocado/avocado.conf
[datadir.paths]
logs_dir = $logs_prefix/ftest/avocado/job-results
data_dir = $logs_prefix/ftest/avocado/data

[job.output]
loglevel = DEBUG

[runner.timeout]
after_interrupted = 120
process_alive = 120
process_died = 120

[sysinfo.collectibles]
files = \$HOME/.config/avocado/sysinfo/files
# File with list of commands that will be executed and have their output
# collected
commands = \$HOME/.config/avocado/sysinfo/commands
EOF

mkdir -p ~/.config/avocado/sysinfo/
cat <<EOF > ~/.config/avocado/sysinfo/commands
ps axf
dmesg
df -h
EOF

cat <<EOF > ~/.config/avocado/sysinfo/files
/proc/mounts
EOF

# apply patches to Avocado
pydir=""
for loc in /usr/lib/python2*/site-packages/ \
           /usr/lib/python3*/site-packages/ \
           /usr/local/lib/python3*/site-packages/; do
    if [ -f "$loc"/avocado/core/runner.py ]; then
        pydir=$loc
        break
    fi
done
if [ -z "${pydir}" ]; then
    echo "Could not determine avocado installation location"
    exit 1
fi

PATCH_DIR="$PREFIX"/lib/daos/TESTING/ftest
# https://github.com/avocado-framework/avocado/pull/4345 fixed somewhere
# before 69.2
if grep "self.job.result_proxy.notify_progress(False)" \
    "$pydir"/avocado/core/runner.py; then
    echo "Applying patch avocado-job-result_proxy-reference-fix.patch"
    if ! cat < "$PATCH_DIR"/avocado-job-result_proxy-reference-fix.patch | \
      sudo patch -p1 -d "$pydir"; then
        echo "Failed to apply avocado PR-4345 patch"
        exit 1
    fi
fi
# https://github.com/avocado-framework/avocado/pull/2908 fixed in
# https://github.com/avocado-framework/avocado/pull/3076/
if ! grep "runner.timeout.process_died" "$pydir"/avocado/core/runner.py; then
    # this version of runner.py is older than 82.0
    if ! grep TIMEOUT_TEARDOWN "$pydir"/avocado/core/runner.py; then
        echo "Applying patch avocado-teardown-timeout.patch"
        if ! cat < "$PATCH_DIR"/avocado-teardown-timeout.patch | \
        sudo patch -p1 -d "$pydir"; then
            echo "Failed to apply avocado PR-3076 patch"
            exit 1
        fi
    fi
fi
# https://github.com/avocado-framework/avocado/pull/3154 - fixed somewhere
# before 69.2
if ! grep "def phase(self)" \
    "$pydir"/avocado/core/test.py; then
    echo "Applying patch avocado-report-test-phases-common.patch"
    if ! filterdiff -p1 -x selftests/* <                       \
        "$PATCH_DIR"/avocado-report-test-phases-common.patch | \
      sed -e '/selftests\/.*/d' |                              \
      sudo patch -p1 -d "$pydir"; then
        echo "Failed to apply avocado PR-3154 patch - common portion"
        exit 1
    fi
    if grep "^TEST_STATE_ATTRIBUTES = " "$pydir"/avocado/core/test.py; then
        echo "Applying patch avocado-report-test-phases-py3.patch"
        if ! cat < "$PATCH_DIR"/avocado-report-test-phases-py3.patch | \
          sudo patch -p1 -d "$pydir"; then
            echo "Failed to apply avocado PR-3154 patch - py3 portion"
            exit 1
        fi
    else
        echo "Applying patch avocado-report-test-phases-py2.patch"
        if ! cat < "$PATCH_DIR"/avocado-report-test-phases-py2.patch | \
          sudo patch -p1 -d "$pydir"; then
            echo "Failed to apply avocado PR-3154 patch - py2 portion"
            exit 1
        fi
    fi
fi
# apply fix for https://github.com/avocado-framework/avocado/issues/2908 - fixed
# somewhere before 69.2
if grep "TIMEOUT_TEST_INTERRUPTED" \
    "$pydir"/avocado/core/runner.py; then
        sudo ed <<EOF "$pydir"/avocado/core/runner.py
/TIMEOUT_TEST_INTERRUPTED/s/[0-9]*$/60/
wq
EOF
fi
# apply fix for https://jira.hpdd.intel.com/browse/DAOS-6756 for avocado 69.x -
# fixed somewhere before 82.0
if grep "TIMEOUT_PROCESS_DIED" \
    "$pydir"/avocado/core/runner.py; then
        sudo ed <<EOF "$pydir"/avocado/core/runner.py
/TIMEOUT_PROCESS_DIED/s/[0-9]*$/60/
wq
EOF
fi
# apply fix for https://github.com/avocado-framework/avocado/pull/2922 - fixed
# somewhere before 69.2
if grep "testsuite.setAttribute('name', 'avocado')" \
    "$pydir"/avocado/plugins/xunit.py; then
    sudo ed <<EOF "$pydir"/avocado/plugins/xunit.py
/testsuite.setAttribute('name', 'avocado')/s/'avocado'/os.path.basename(os.path.dirname(result.logfile))/
wq
EOF
fi
# Fix for bug to be filed upstream - fixed somewhere before 69.2
if grep "self\.job\.result_proxy\.notify_progress(False)" \
    "$pydir"/avocado/core/runner.py; then
    sudo ed <<EOF "$pydir"/avocado/core/runner.py
/self\.job\.result_proxy\.notify_progress(False)/d
wq
EOF
fi

pushd "$PREFIX"/lib/daos/TESTING/ftest

# make sure no lingering corefiles or junit files exist
rm -f core.* ./*_results.xml

# see if we just wanted to set up
if ${SETUP_ONLY:-false}; then
    exit 0
fi

# check if slurm needs to be configured for soak
if [[ "${TEST_TAG_ARG}" =~ soak ]]; then
    if ! ./slurm_setup.py -d -c "$FIRST_NODE" -n "${TEST_NODES}" -s -i; then
        exit "${PIPESTATUS[0]}"
    else
        rc=0
    fi
fi

# need to increase the number of oopen files (on EL8 at least)
ulimit -n 4096

launch_args="-jcrisa"
# processing cores is broken on EL7 currently
id="$(lsb_release -si)"
if { [ "$id" = "CentOS" ]                 &&
     [[ $(lsb_release -s -r) != 7.* ]]; } ||
   [ "$id" = "AlmaLinux" ]                ||
   [ "$id" = "Rocky" ]                    ||
   [ "$id" = "RedHatEnterpriseServer" ]   ||
   [ "$id" = "openSUSE" ]; then
    launch_args+="p"
fi

# Clean stale job results
if [ -d "${logs_prefix}/ftest/avocado/job-results" ]; then
    rm -rf "${logs_prefix}/ftest/avocado/job-results"
fi

# now run it!
# shellcheck disable=SC2086
export WITH_VALGRIND
export STAGE_NAME
# shellcheck disable=SC2086
if ! ./launch.py "${launch_args}" -th "${LOGS_THRESHOLD}" \
                 -ts "${TEST_NODES}" ${LAUNCH_OPT_ARGS} ${TEST_TAG_ARR[*]}; then
    rc=${PIPESTATUS[0]}
else
    rc=0
fi

# daos_test uses cmocka framework which generates a set of xml of its own.
# Post-processing the xml files here to put them in proper categories
# for publishing in Jenkins
TEST_DIRS=("daos_test" "checksum")

for test_dir in "${TEST_DIRS[@]}"; do
    COMP="FTEST_${test_dir}"
    if [[ "${LAUNCH_OPT_ARGS}" == *"--repeat="* ]]; then
        FILES=("${logs_prefix}/ftest/avocado/job-results/${test_dir}"/*/test-results/*/*/data/*.xml)
    else
        FILES=("${logs_prefix}/ftest/avocado/job-results/${test_dir}"/*/test-results/*/data/*.xml)
    fi
    ./scripts/post_process_xml.sh "${COMP}" "${FILES[@]}"
done

exit $rc
