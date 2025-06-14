#!/bin/bash

set -eux

# Generate code coverage report
if [[ -n $(find . -name "code_coverage.json") ]]; then
    mkdir -p code_coverage_report
    # jsonfiles=$(find * -name "code_coverage.json").split()
    # gcovr --add-tracefile "*/code_coverage/code_coverage.json" \
    # test only
    gcovr --add-tracefile "unit_test_logs/code_coverage/code_coverage.json" \
          --add-tracefile "unit_test_bdev_logs/code_coverage/code_coverage.json" \
          --output code_coverage_report/code_coverage_report.html \
          --html-details --gcov-ignore-parse-errors
fi
