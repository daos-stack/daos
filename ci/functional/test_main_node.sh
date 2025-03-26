#!/bin/bash

set -eux

DAOS_TEST_SHARED_DIR=$(mktemp -d -p /mnt/share/)
trap 'rm -rf $DAOS_TEST_SHARED_DIR' EXIT

export DAOS_TEST_SHARED_DIR
export TEST_RPMS=true
export REMOTE_ACCT=jenkins
export WITH_VALGRIND="$WITH_VALGRIND"
export STAGE_NAME="$STAGE_NAME"

touch ~/.ssh/known_hosts
chmod 600 ~/.ssh/known_hosts
for host in $(echo $TNODES | tr "," "\n")
do
    echo "Removing all keys from known_hosts file for $host"
    ssh-keygen -R $host
    echo "Add new key for $host to known_hosts"
    ssh-keyscan $host >> ~/.ssh/known_hosts
done

/usr/lib/daos/TESTING/ftest/ftest.sh "$TEST_TAG" "$TNODES" "$FTEST_ARG"
