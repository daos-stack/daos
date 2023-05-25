#!/bin/sh

set -e

ls -l

git config --global --add safe.directory /github/workspace

git-clang-format ${INPUT_TARGET}

git diff
