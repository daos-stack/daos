#!/bin/bash
# /*
#  * (C) Copyright 2016-2022 Intel Corporation.
#  * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
#  */

set -ex -o pipefail

# shellcheck disable=SC1091
if [ -f .localenv ]; then
    # read (i.e. environment, etc.) overrides
    . .localenv
fi

TEST_TAG_ARG="${1:-quick}"

TEST_TAG_DIR="$(mktemp -du)"

NFS_SERVER=${NFS_SERVER:-${HOSTNAME%%.*}}

trap 'echo "encountered an unchecked return code, exiting with error"' ERR

IFS=" " read -r -a nodes <<< "${2//,/ }"
TEST_NODES=$(IFS=","; echo "${nodes[*]:1}")

# Optional arguments for launch.py
LAUNCH_OPT_ARGS="${3:-}"

# Add the missing '--nvme' argument identifier for backwards compatibility with
# the 'auto:Optane' optional argument specified without the identifier.
LAUNCH_OPT_ARGS="$(echo "$LAUNCH_OPT_ARGS" | sed -e 's/^/ /' -e 's/ \(auto:-3DNAND\)/--nvme=\1/' -e 's/^ *//')"

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
    if [ $i -eq 0 ]; then
        echo "All pre clean nodes attempts failed." >&2
        return 1
    fi
    return 0
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
    if [ $i -eq 0 ]; then
        echo "All cleanup attempts failed." >&2
        return 1
    fi
    return 0
}

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

pre_clean

if ${TEARDOWN_ONLY:-false}; then
    cleanup
    exit 0
fi

trap 'set +e; cleanup' EXIT

# doesn't work: mapfile -t CLUSH_ARGS <<< "$CLUSH_ARGS"
# shellcheck disable=SC2206
CLUSH_ARGS=($CLUSH_ARGS)

DAOS_BASE=${SL_SRC_DIR}
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
     LAUNCH_OPT_ARGS=\"$LAUNCH_OPT_ARGS\"
     WITH_VALGRIND=\"$WITH_VALGRIND\"
     STAGE_NAME=\"$STAGE_NAME\"
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
