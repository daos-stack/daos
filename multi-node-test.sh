#!/bin/bash
# Copyright (C) 2019 Intel Corporation
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

set -ex -o pipefail

# shellcheck disable=SC1091
if [ -f .localenv ]; then
    # read (i.e. environment, etc.) overrides
    . .localenv
fi

NFS_SERVER=${NFS_SERVER:-${HOSTNAME%%.*}}

# A list of tests to run as a two node instance on Jenkins
JENKINS_TEST_LIST_2=(scripts/cart_echo_test.yml                   \
                     scripts/cart_echo_test_non_sep.yml           \
                     scripts/cart_self_test.yml                   \
                     scripts/cart_self_test_non_sep.yml           \
                     scripts/cart_test_corpc_prefwd.yml           \
                     scripts/cart_test_corpc_prefwd_non_sep.yml   \
                     scripts/cart_test_group.yml                  \
                     scripts/cart_test_group_non_sep.yml          \
                     scripts/cart_test_barrier.yml                \
                     scripts/cart_test_barrier_non_sep.yml        \
                     scripts/cart_threaded_test.yml               \
                     scripts/cart_threaded_test_non_sep.yml       \
                     scripts/cart_test_rpc_error.yml              \
                     scripts/cart_test_rpc_error_non_sep.yml      \
                     scripts/cart_test_singleton.yml              \
                     scripts/cart_test_singleton_non_sep.yml      \
                     scripts/cart_rpc_test.yml                    \
                     scripts/cart_rpc_test_non_sep.yml            \
                     scripts/cart_test_corpc_version.yml          \
                     scripts/cart_test_corpc_version_non_sep.yml  \
                     scripts/cart_test_iv.yml                     \
                     scripts/cart_test_iv_non_sep.yml             \
                     scripts/cart_test_proto.yml                  \
                     scripts/cart_test_proto_non_sep.yml          \
                     scripts/cart_test_no_timeout.yml             \
                     scripts/cart_test_no_timeout_non_sep.yml)

# A list of tests to run as a three node instance on Jenkins
JENKINS_TEST_LIST_3=(scripts/cart_test_group_tiers.yml)

# A list of tests to run as a five node instance on Jenkins
JENKINS_TEST_LIST_5=(scripts/cart_test_cart_ctl.yml      \
                     scripts/cart_test_corpc_prefwd.yml  \
                     scripts/cart_test_corpc_version.yml \
                     scripts/cart_test_barrier.yml)

trap 'echo "encountered an unchecked return code, exiting with error"' ERR

# shellcheck disable=SC1091
. .build_vars-Linux.sh

IFS=" " read -r -a nodes <<< "${2//,/ }"

log_base_path="testLogs-${1}_node"

rm -f results_1.yml CART_[235]-node_junit.xml

# shellcheck disable=SC1004
# shellcheck disable=SC2154
trap 'set +e
i=5
while [ $i -gt 0 ]; do
    pdsh -l jenkins -R ssh -S \
         -w "$(IFS=','; echo ${nodes[*]:0:$1})" "set -x
    x=0
    rc=0
    while [ \$x -lt 30 ] &&
          grep $CART_BASE /proc/mounts &&
          ! sudo umount $CART_BASE; do
        # CART-558 - cart tests leave orte processes around
        if pgrep -a cart_ctl; then
            echo "FAIL: cart_ctl found left running on \$HOSTNAME"
            rc=1
        fi
        pgrep -a \(orte-dvm\|orted\|cart_ctl\)
        pkill \(orte-dvm\|orted\|cart_ctl\)
        sleep 1
        let x+=1
    done
    sudo sed -i -e \"/added by multi-node-test-$1.sh/d\" /etc/fstab
    sudo rmdir $CART_BASE || find $CART_BASE || true
    if [ \$rc != 0 ]; then
        exit \$rc
    fi" 2>&1 | dshbak -c
    if [ ${PIPESTATUS[0]} = 0 ]; then
        i=0
    fi
    let i-=1
done' EXIT

CART_BASE=${SL_OMPI_PREFIX%/install/*}
if ! pdsh -l jenkins -R ssh -S \
          -w "$(IFS=','; echo "${nodes[*]:0:$1}")" "set -ex
ulimit -c unlimited
sudo mkdir -p $CART_BASE
sudo ed <<EOF /etc/fstab
\\\$a
$NFS_SERVER:$PWD $CART_BASE nfs defaults 0 0 # added by multi-node-test-$1.sh
.
wq
EOF
sudo mount $CART_BASE

# TODO: package this in to an RPM
pip3 install --user tabulate

df -h" 2>&1 | dshbak -c; then
    echo "Cluster setup (i.e. provisioning) failed"
    exit 1
fi

# shellcheck disable=SC2029
if ! ssh -i ci_key jenkins@"${nodes[0]}" "set -ex
ulimit -c unlimited
cd $CART_BASE

# now run it!
pushd install/Linux/TESTING/
# TODO: this needs DRYing out into a single block that can handle
# any number of nodes
if [ \"$1\" = \"2\" ]; then
    cat <<EOF > scripts/cart_multi_two_node.cfg
{
    \"with_test_runner_host_list\": [\"${nodes[1]}\",
                                     \"${nodes[2]}\"],
    \"host_list\": [\"${nodes[0]}\",
                    \"${nodes[1]}\"],
    \"use_daemon\": \"DvmRunner\",
    \"log_base_path\": \"$log_base_path\"
}
EOF
    rm -rf $log_base_path/
    python3 test_runner config=scripts/cart_multi_two_node.cfg \\
        ${JENKINS_TEST_LIST_2[*]} || {
        rc=\${PIPESTATUS[0]}
        echo \"Test exited with \$rc\"
    }
    find $log_base_path/testRun -name subtest_results.yml \\
         -exec grep -Hi fail {} \\;
elif [ \"$1\" = \"3\" ]; then
    cat <<EOF > scripts/cart_multi_three_node.cfg
{
    \"with_test_runner_host_list\": [
        \"${nodes[1]}\",
        \"${nodes[2]}\",
        \"${nodes[3]}\"
    ],
    \"host_list\": [\"${nodes[0]}\",
        \"${nodes[1]}\",
        \"${nodes[2]}\"
    ],
    \"use_daemon\": \"DvmRunner\",
    \"log_base_path\": \"$log_base_path\"
}
EOF
    rm -rf $log_base_path/
    python3 test_runner config=scripts/cart_multi_three_node.cfg \\
        ${JENKINS_TEST_LIST_3[*]} || {
        rc=\${PIPESTATUS[0]}
        echo \"Test exited with \$rc\"
    }
    find $log_base_path/testRun -name subtest_results.yml \\
         -exec grep -Hi fail {} \\;
elif [ \"$1\" = \"5\" ]; then
    cat <<EOF > scripts/cart_multi_five_node.cfg
{
    \"with_test_runner_host_list\": [
        \"${nodes[1]}\",
        \"${nodes[2]}\",
        \"${nodes[3]}\",
        \"${nodes[4]}\",
        \"${nodes[5]}\"
    ],
    \"host_list\": [\"${nodes[0]}\",
        \"${nodes[1]}\",
        \"${nodes[2]}\",
        \"${nodes[3]}\",
        \"${nodes[4]}\"
    ],
    \"use_daemon\": \"DvmRunner\",
    \"log_base_path\": \"$log_base_path\"
}
EOF
    rm -rf $log_base_path/
    python3 test_runner config=scripts/cart_multi_five_node.cfg \\
        ${JENKINS_TEST_LIST_5[*]} || {
        rc=\${PIPESTATUS[0]}
        echo \"Test exited with \$rc\"
    }
    find $log_base_path/testRun -name subtest_results.yml \\
         -exec grep -Hi fail {} \\;
fi
exit \$rc"; then
    rc=${PIPESTATUS[0]}
else
    rc=0
fi

scp -i ci_key -r jenkins@"${nodes[0]}:\
$CART_BASE/install/Linux/TESTING/$log_base_path" install/Linux/TESTING/
{
    cat <<EOF
TestGroup:
    submission: $(TZ=UTC date)
    test_group: CART_${1}-node
    testhost: $HOSTNAME
    user_name: jenkins
Tests:
EOF
    find install/Linux/TESTING/"$log_base_path" \
         -name subtest_results.yml -print0 | xargs -0 cat
} > results_1.yml

if ! PYTHONPATH=scony_python-junit/ \
       jenkins/autotest_utils/results_to_junit.py; then
    echo "Failed to convert YML to Junit"
    if [ "$rc" = "0" ]; then
        rc=1
    fi
fi

exit "$rc"
