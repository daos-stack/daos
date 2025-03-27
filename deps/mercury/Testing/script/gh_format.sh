#!/bin/bash

set -e

if [ -z "$1" ]
then
  echo "Error: no commit passed"
  exit 1
fi

# Check C and C++ code with clang-format
echo "Checking formatting for commit range: $1"
DIFF="$(git clang-format-${CLANG_FORMAT_VERSION} --style file --diff --extensions c,h $1)"
if [ -n "${DIFF}" ] && [ "${DIFF}" != "no modified files to format" ] && [ "${DIFF}" != "clang-format did not modify any files" ]
then
  echo "clang-format:"
  echo "  Code format checks failed."
  echo "  Please run clang-format (or git clang-format) on your changes"
  echo "  before committing."
  echo "  The following changes are suggested:"
  echo "${DIFF}"
  exit 1
fi

