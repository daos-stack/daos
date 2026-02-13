#!/bin/bash
#
#  Copyright 2026 Hewlett Packard Enterprise Development LP
#
# Script for generating a bullseye code coverage report summary
set -uex

if [ ! -d '/opt/BullseyeCoverage/bin' ]; then
  echo 'Bullseye not found.'
  exit 1
fi
export COVFILE="${WORKSPACE:-/tmp}/test.cov"
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
fi

# Generate the html report
rm -fr bullseye_code_coverage_report || true
mkdir bullseye_code_coverage_report
cp /opt/BullseyeCoverage/daos/bullseye_sources.tar.gz .
tar -xf bullseye_sources.tar.gz
covhtml --srcdir . --file test.cov bullseye_code_coverage_report
ls -al bullseye_code_coverage_report
