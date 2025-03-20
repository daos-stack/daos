#!/bin/bash
set -xe
name=$1
version=$2

old_branch=$(git branch --show-current)
git checkout -B tmp_branch_resolve
trap "git checkout ${old_branch}; git branch -D tmp_branch_resolve" EXIT
./resolve_patches.sh spdk
pushd ../../ || exit 1
utils/rpms/archive.sh utils/rpms "${name}" "${version}" tar
popd || exit
