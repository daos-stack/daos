#!/bin/bash
# Copyright (C) Copyright 2019-2020 Intel Corporation
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

TEST_TAG_ARG="${1:-quick}"

TEST_TAG_DIR="/tmp/Functional_${TEST_TAG_ARG// /_}"

NFS_SERVER=${NFS_SERVER:-${HOSTNAME%%.*}}

trap 'echo "encountered an unchecked return code, exiting with error"' ERR

IFS=" " read -r -a nodes <<< "${2//,/ }"
TEST_NODES=$(IFS=","; echo "${nodes[*]:1:8}")

# Optional --nvme argument for launch.py
NVME_ARG=""
if [ -n "${3}" ]; then
    NVME_ARG="-n ${3}"
fi

# Log size threshold
LOGS_THRESHOLD="1G"

# For nodes that are only rebooted between CI nodes left over mounts
# need to be cleaned up.
pre_clean () {
    i=5
    while [ $i -gt 0 ]; do
        if clush "${CLUSH_ARGS[@]}" -B -l "${REMOTE_ACCT:-jenkins}" -R ssh \
                 -S -w "$(IFS=','; echo "${nodes[*]}")"                    \
                 "$(sed -e '1,/^$/d' "$SCRIPT_LOC"/pre_clean_nodes.sh)"; then
            break
        fi
        ((i-=1)) || true
    done
}

cleanup() {
    i=5
    while [ $i -gt 0 ]; do
        if clush "${CLUSH_ARGS[@]}" -B -l "${REMOTE_ACCT:-jenkins}" -R ssh \
             -S -w "$(IFS=','; echo "${nodes[*]}")"                        \
             "DAOS_BASE=$DAOS_BASE
             $(sed -e '1,/^$/d' "$SCRIPT_LOC"/cleanup_nodes.sh)"; then
            break
        fi
        ((i-=1)) || true
    done
}

pre_clean

# shellcheck disable=SC1091
if ${TEST_RPMS:-false}; then
    PREFIX=/usr
    SL_PREFIX=$PWD
else
    TEST_RPMS=false
    PREFIX=install
    . .build_vars.sh
fi

SCRIPT_LOC="$PREFIX"/lib/daos/TESTING/ftest/scripts

if ${TEARDOWN_ONLY:-false}; then
    cleanup
    exit 0
fi

trap 'set +e; cleanup' EXIT

# doesn't work: mapfile -t CLUSH_ARGS <<< "$CLUSH_ARGS"
# shellcheck disable=SC2206
CLUSH_ARGS=($CLUSH_ARGS)

DAOS_BASE=${SL_PREFIX%/install}
if ! clush "${CLUSH_ARGS[@]}" -B -l "${REMOTE_ACCT:-jenkins}" -R ssh -S \
    -w "$(IFS=','; echo "${nodes[*]}")"                                 \
    "FIRST_NODE=${nodes[0]}
     TEST_RPMS=$TEST_RPMS
     DAOS_BASE=$DAOS_BASE
     SL_PREFIX=$SL_PREFIX
     TEST_TAG_DIR=$TEST_TAG_DIR
     JENKINS_URL=$JENKINS_URL
     NFS_SERVER=$NFS_SERVER
     $(sed -e '1,/^$/d' "$SCRIPT_LOC"/setup_nodes.sh)"; then
    echo "Cluster setup (i.e. provisioning) failed"
    exit 1
fi

args="${1:-quick}"
shift || true
args+=" $*"

# shellcheck disable=SC2029
# shellcheck disable=SC2086
if ! ssh -A $SSH_KEY_ARGS ${REMOTE_ACCT:-jenkins}@"${nodes[0]}" \
    "FIRST_NODE=\"${nodes[0]}\"
     TEST_RPMS=\"$TEST_RPMS\"
     DAOS_TEST_SHARED_DIR=\"${DAOS_TEST_SHARED_DIR:-$PWD/install/tmp}\"
     DAOS_BASE=\"$DAOS_BASE\"
     TEST_TAG_DIR=\"$TEST_TAG_DIR\"
     PREFIX=\"$PREFIX\"
     SETUP_ONLY=\"${SETUP_ONLY:-false}\"
     TEST_TAG_ARG=\"$TEST_TAG_ARG\"
     TEST_NODES=\"$TEST_NODES\"
     NVME_ARG=\"$NVME_ARG\"
     LOGS_THRESHOLD=\"$LOGS_THRESHOLD\"
     $(sed -e '1,/^$/d' "$SCRIPT_LOC"/main.sh)"; then
    rc=${PIPESTATUS[0]}
    if ${SETUP_ONLY:-false}; then
        exit "$rc"
    fi
else
    if ${SETUP_ONLY:-false}; then
        trap '' EXIT
        exit 0
    fi
    rc=0
fi

exit "$rc"
