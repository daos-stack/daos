#!/bin/bash

set -eux

if ! DAOS_TEST_SHARED_DIR=$(mktemp -d -p /mnt/share/); then
    echo "Failed to create temp dir in /mnt/share:"
    ls -ld /mnt/share
    id
    exit 1
fi
trap 'rm -rf $DAOS_TEST_SHARED_DIR' EXIT

export DAOS_TEST_SHARED_DIR
export TEST_RPMS
export REMOTE_ACCT

/usr/lib/daos/TESTING/ftest/ftest.sh "$TEST_TAG" "$TNODES" "$FTEST_ARG"
