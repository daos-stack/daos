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
if [ -d bullseye_code_coverage_report ]; then
  rm -fr bullseye_code_coverage_report
fi
mkdir -p bullseye_code_coverage_report
pushd bullseye_code_coverage_report

mkdir -p report
pushd report
mkdir -p sources
tar -xf /opt/BullseyeCoverage/daos/bullseye_sources.tar.gz -C sources/
covhtml --srcdir sources --file "${COVFILE}" .
popd

tar -czf bullseye_code_coverage_report.tar.gz -C report .
rm -fr report

cp "${COVFILE}" .
popd
