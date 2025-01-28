#!/bin/bash

set -eux

if [ ! -d '/opt/BullseyeCoverage/bin' ]; then
  echo '#Bullseye not found.'
  exit 1
fi
export COVFILE="$WORKSPACE/test.cov"
export PATH="/opt/BullseyeCoverage/bin:$PATH"

echo "===>"
pwd
ls -la
echo "<---"

mv "$WORKSPACE/test.cov_1" "$COVFILE"
if [ -e "$WORKSPACE/test.cov_2" ]; then
  covmerge --no-banner --file "$COVFILE" "$WORKSPACE"/test.cov_*
fi

if [ ! -e "$COVFILE" ]; then
  echo "#Coverage file $COVFILE is missing"
else
  ls -l "$COVFILE"
fi

#To Do: symlink bullseye/src
#  ie: ln -vs ~/daos/src ~/BullseyeCoverage/src
#To remove test.cov after java -jar
mkdir -p test_coverage_report
java -jar bullshtml.jar "$WORKSPACE"/test_coverage_report
