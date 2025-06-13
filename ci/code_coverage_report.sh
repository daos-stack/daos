#!/bin/bash

set -eux

# ls -al
# Generate code coverage report
if [[ -n $(find . -name "code_coverage.json") ]]; then
    mkdir -p code_coverage_report
    jsonfiles=$(find * -name "code_coverage.json")
    gcovr --json-add-tracefile $jsonfiles \
          --output code_coverage_report/code_coverage_report.html \
          --html-details --gcov-ignore-parse-errors
fi
