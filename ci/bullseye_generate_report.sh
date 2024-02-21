#!/bin/bash

set -eux

if [ ! -d '/opt/BullseyeCoverage/bin' ]; then
  echo '#Bullseye not found.'
  exit 1
fi
export COVFILE="$WORKSPACE/test.cov"
export PATH="/opt/BullseyeCoverage/bin:$PATH"

echo "======>"
pwd
ls
ls "/var/tmp/ftest/avocado/job-results/bullseye_coverage_logs/"
ls "$WORKSPACE/"
echo "<======"

# Decompress any zipped bullseye files, e.g.
#   $WORKSPACE/Functional */bullseye_coverage_logs/test.*.cov.bz2
find "$WORKSPACE" -maxdepth 3 -type f -name 'test.*.cov.bz2' \
  -print0 | sudo -n xargs -0 -r0 lbunzip2 -v -k

# Merge all of the bullseye files into one, e.g.
#   $WORKSPACE/covc_test_logs/test.cov
#   $WORKSPACE/Functional */bullseye_coverage_logs/test.*.cov
echo "Merging the following bullseye files"
find "$WORKSPACE" -maxdepth 3 -type f -name 'test*.cov'
find "$WORKSPACE" -maxdepth 3 -type f -name 'test*.cov' \
  -print0 | sudo -n xargs -0 -r0 covmerge --no-banner --create --file "$COVFILE"

# Remove decompressed bullseye files after merge
find "$WORKSPACE" -maxdepth 3 -type f -name 'test.*.cov' -print -delete

if [ ! -e "$COVFILE" ]; then
  echo "#Coverage file $COVFILE is missing"
else
  ls -l "$COVFILE"
  mkdir -p "$WORKSPACE"/test_coverage_report/
fi

#To Do: symlink bullseye/src
#  ie: ln -vs ~/daos/src ~/BullseyeCoverage-9.5.13/src
java -jar bullshtml.jar "$WORKSPACE"/test_coverage_report
