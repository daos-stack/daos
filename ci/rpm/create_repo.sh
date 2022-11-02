#!/bin/bash

set -eux

# shellcheck disable=SC1091
. ci/gha_functions.sh

repo_path=$(repo_serial_increment)
mkdir -p "$repo_path$TARGET"
cp -a "$BUILD_CHROOT"/result/*.rpm "$repo_path$TARGET"
mock -r rocky+epel-8-x86_64 --scrub=all --uniqueext "$RUN_ID"
cd "$repo_path$TARGET"
createrepo .
