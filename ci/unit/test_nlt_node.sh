#!/bin/bash

# This is a script to be run by the ci/unit/test_nlt.sh to run a test
# on a CI node.

set -uex

sudo bash -c 'echo 1 > /proc/sys/kernel/sysrq'
sudo mkdir -p /mnt/daos

# Create the directory path as root, then replace the actual directory itself
# with a symlink back to the data.  This avoids a copy, and allows the remote
# node to pull from a known location.
sudo mkdir -p "$DAOS_BASE"
sudo ln -sF "$(readlink -f build)/install" "$DAOS_BASE"

cd build

# Setup daos admin etc.
sudo bash -c ". ./utils/sl/setup_local.sh; ./utils/setup_daos_admin.sh"

# NLT will mount /mnt/daos itself.
./utils/node_local_test.py all
