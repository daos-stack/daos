#!/bin/sh

set -e

git config --global --add safe.directory /github/workspace

# Show what would be changed as a diff.
git-clang-format "${INPUT_TARGET}" --diffstat || true
git-clang-format "${INPUT_TARGET}" --diff
