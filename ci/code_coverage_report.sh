#!/bin/bash

# Use default python as that's where storage_estimator is installed.
python3.11 -m venv venv
# shellcheck disable=SC1091
source venv/bin/activate
touch venv/pip.conf
pip config set global.progress_bar off
pip config set global.no_color true
pip install --upgrade pip
pip install --requirement requirements-coverage.txt

# Generate code coverage report
if [[ -n $(find build -name "*/code_coverage.json") ]]; then
    mkdir -p "${test_log_dir}/code_coverage"
    gcovr --json-add-tracefile "*/code_coverage.json" -o code_coverage/code_coverage_report.html \
          --html-details --gcov-ignore-parse-errors
fi
