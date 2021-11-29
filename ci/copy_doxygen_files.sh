#!/bin/bash

# Script to run doxygen against modified headers and report results.
# see .github/workflows/doxygen.yml
# Just run against all installed headers, use github actions trigger to control when
# job runs.

set -e

echo ::group::Installing doxygen
sudo apt-get install doxygen
echo ::endgroup::

echo ::group::Running check
echo ::add-matcher::ci/daos-doxygen-matcher.json
doxygen Doxyfile
echo ::remove-matcher owner=daos-doxygen::
echo ::endgroup::
