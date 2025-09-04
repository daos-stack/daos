#!/bin/bash

set -eux

# Generate code coverage report
if [[ -n $(find . -name "details.json") ]]; then
    echo "details.json files found"
fi
