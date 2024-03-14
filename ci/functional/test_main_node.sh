#!/bin/bash

set -eux

DAOS_TEST_SHARED_DIR=$(mktemp -d -p /mnt/share/)
trap 'rm -rf $DAOS_TEST_SHARED_DIR' EXIT

export DAOS_TEST_SHARED_DIR
export TEST_RPMS=true
export REMOTE_ACCT=jenkins
export WITH_VALGRIND="$WITH_VALGRIND"
export STAGE_NAME="$STAGE_NAME"

if [ -n "$BULLSEYE" ]; then
    export PATH="/opt/BullseyeCoverage/bin:$PATH"
fi
/usr/lib/daos/TESTING/ftest/ftest.sh "$TEST_TAG" "$TNODES" "$FTEST_ARG"
