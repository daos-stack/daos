#!/bin/sh

set -e

git config --global --add safe.directory /github/workspace

git-clang-format "${INPUT_TARGET}" --diff || true

git diff
