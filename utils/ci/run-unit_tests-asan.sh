#!/bin/bash

set -ueo pipefail

mkdir -p /mnt/daos
mount -t tmpfs -o rw,noatime,inode64,huge=always,mpol=prefer:0,uid=$(id -u),gid=$(id -g) tmpfs /mnt/daos

cd daos
source utils/sl/setup_local.sh
exec env PMEMOBJ_CONF=sds.at_create=0 ddb_tests
