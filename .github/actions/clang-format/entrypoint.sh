#!/bin/sh

set -e

ls -l

git-clang-format ${INPUT_TARGET}

git diff
