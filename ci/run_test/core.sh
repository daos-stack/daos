#!/bin/bash

set -ex

sudo bash -c 'echo 1 > /proc/sys/kernel/sysrq'
if grep /mnt/daos\  /proc/mounts; then
    sudo umount /mnt/daos
else
    sudo mkdir -p /mnt/daos
fi
sudo mount -t tmpfs -o size=16G tmpfs /mnt/daos
sudo mkdir -p "$DAOS_BASE"
sudo mount -t nfs "$HOSTNAME":"$PWD" "$DAOS_BASE"

# copy daos_admin binary into $PATH and fix perms
sudo cp "$DAOS_BASE"/install/bin/daos_admin /usr/bin/daos_admin && \
  sudo chown root /usr/bin/daos_admin && \
  sudo chmod 4755 /usr/bin/daos_admin && \
  mv "$DAOS_BASE"/install/bin/daos_admin \
     "$DAOS_BASE"/install/bin/orig_daos_admin

# set CMOCKA envs here
export CMOCKA_MESSAGE_OUTPUT=xml
export CMOCKA_XML_FILE="$DAOS_BASE"/test_results/%g.xml
cd "$DAOS_BASE"
IS_CI=true OLD_CI=false utils/run_test.sh
./utils/node_local_test.py all | tee test.out

