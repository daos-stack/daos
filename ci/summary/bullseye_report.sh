#!/bin/bash

# Script for installing packages used for CI summary steps
set -uex

#!/bin/bash

# Script for generating a bullseye code coverage report
set -eux

if [ ! -d '/opt/BullseyeCoverage/bin' ]; then
  echo 'Bullseye not found.'
  exit 1
fi
export COVFILE="$WORKSPACE/test.cov"
export PATH="/opt/BullseyeCoverage/bin:$PATH"

# Merge all coverage files
cp /opt/BullseyeCoverage/daos/test.cov "${COVFILE}"
readarray -t cov_files < <(find "${WORKSPACE}" -name test.cov)
if [ ${#cov_files[@]} -gt 0 ]; then
  covmerge --no-banner --file "${COVFILE}" "${cov_files[@]}"
fi

if [ ! -e "$COVFILE" ]; then
  echo "Coverage file ${COVFILE} is missing"
  exit 1
else
  ls -al "${COVFILE}"
  covdir -m
fi

# Generate the html report
# java -jar bullshtml.jar bullseye_code_coverage_report
covhtml bullseye_code_coverage_report
# ls -al bullseye_code_coverage_report

