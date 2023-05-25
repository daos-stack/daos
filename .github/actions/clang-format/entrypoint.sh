#!/bin/sh

ls -l

git-clang-format ${INPUT_TARGET}

git diff
