#!/bin/bash

set -eux

DAOS_TEST_SHARED_DIR=$(mktemp -d -p /mnt/share/)
trap 'rm -rf $DAOS_TEST_SHARED_DIR' EXIT

export DAOS_TEST_SHARED_DIR
export TEST_RPMS=true
export REMOTE_ACCT=jenkins

/usr/lib/daos/TESTING/ftest/ftest.sh "$TEST_TAG" "$TNODES" "$FTEST_ARG"
