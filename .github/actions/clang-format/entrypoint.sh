#!/bin/sh

ls -l

git-clang-format ${target_commit}

git diff
