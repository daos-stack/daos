#!/bin/bash

set -eux

sudo mkdir -p "$DAOS_BASE"
sudo mount -t nfs "$HOSTNAME":"$HOSTPWD" "$DAOS_BASE"
set +x
if [ -n "$BULLSEYE" ]; then
  pushd "$DAOS_BASE/bullseye"
    sudo ./install --quiet --key "${BULLSEYE}" \
                   --prefix /opt/BullseyeCoverage
  popd
  rm -rf bullseye
  export COVFILE="$DAOS_BASE/test.cov"
  export PATH="/opt/BullseyeCoverage/bin:$PATH"
fi

DAOS_TEST_SHARED_DIR=$(mktemp -d -p /mnt/share/)
trap 'rm -rf $DAOS_TEST_SHARED_DIR' EXIT

export DAOS_TEST_SHARED_DIR
#export TEST_RPMS=true
export REMOTE_ACCT=jenkins

if $TEST_RPMS; then
  /usr/lib/daos/TESTING/ftest/ftest.sh "$TEST_TAG" "$TNODES" "$FTEST_ARG"
else
  echo SCHAN15 $PWD
  ls $DAOS_BASE
  $DAOS_BASE/ftest.sh "$TEST_TAG" "$TNODES" "$FTEST_ARG"
fi
