#!/bin/bash

artdir=%s
chroot_name=%s

rm -rf artifacts/$artdir/
mkdir -p artifacts/$artdir/
make CHROOT_NAME=$chroot_name -C utils/rpms chrootbuild