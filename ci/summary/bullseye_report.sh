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

readarray -t cov_files < <(find "${WORKSPACE}" -name test.cov)
if [ ${#cov_files[@]} -gt 0 ]; then
  covmerge --no-banner --file "${COVFILE}" "${cov_files[@]}"
fi

if [ ! -e "$COVFILE" ]; then
  echo "Coverage file ${COVFILE} is missing"
else
  ls -al "${COVFILE}"
fi

java -jar bullshtml.jar bullseye_code_coverage_report
ls -al bullseye_code_coverage_report
