#!/bin/bash

set -ex

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

touch "$COVFILE"
covmerge --no-banner --file "$COVFILE" "$DAOS_BASE"/test.cov_*

if [ -n "$BULLSEYE" ]; then
  ls -l "$COVFILE" || true
  java -jar bullshtml.jar test_coverage
fi
