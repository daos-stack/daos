#!/bin/bash

set -eux

# shellcheck disable=SC1091
. ci/gha_functions.sh

repo_path=$(get_repo_path)
mkdir -p "$repo_path$TARGET"
cp -a "$BUILD_CHROOT"/result/*.rpm "$repo_path$TARGET"
mock -r "$CHROOT_NAME" --scrub=all --uniqueext "$RUN_ID"
cd "$repo_path$TARGET"
createrepo .
