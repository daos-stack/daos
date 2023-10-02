#!/bin/bash

set -eux

if [ ! -d '/opt/BullseyeCoverage/bin' ]; then
  echo 'Bullseye not found.'
  exit 1
fi
export COVFILE="$WORKSPACE/test.cov"
export PATH="/opt/BullseyeCoverage/bin:$PATH"

mv "$WORKSPACE/test.cov_1" "$COVFILE"
if [ -e "$WORKSPACE/test.cov_2" ]; then
  covmerge --no-banner --file "$COVFILE" "$WORKSPACE"/test.cov_*
fi

if [ ! -e "$COVFILE" ]; then
  echo "Coverage file $COVFILE is missing"
else
  ls -l "$COVFILE"
fi

java -jar bullshtml.jar test_coverage
