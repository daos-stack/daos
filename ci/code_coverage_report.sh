#!/bin/bash

set -eux

# Generate code coverage report
if [[ -n $(find . -name "code_coverage.json") ]]; then
    sudo dnf install -y python3.11 python3.11-devel
    python3.11 -m venv venv
    # shellcheck disable=SC1091
    source venv/bin/activate
    touch venv/pip.conf
    pip config set global.progress_bar off
    pip config set global.no_color true
    pip install --upgrade pip
    pip install --requirement requirements-code-coverage.txt

    mkdir -p code_coverage_report
    gcovr --add-tracefile "*/code_coverage/code_coverage.json" \
          --output code_coverage_report/code_coverage_report.html \
          --html-details --gcov-ignore-parse-errors
fi
