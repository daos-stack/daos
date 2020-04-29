#!/bin/bash

set -ex

rm -rf artifacts/"${artdir:?}"/
mkdir -p artifacts/"${artdir:?}"/
make CHROOT_NAME="${chroot_name:?}" -C utils/rpms chrootbuild
