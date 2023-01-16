#!/bin/bash
# /*
#  * (C) Copyright 2016-2022 Intel Corporation.
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
# */

set -eux
if grep /mnt/daos /proc/mounts; then
    if ! sudo umount /mnt/daos; then
        echo "During shutdown, failed to unmount /mnt/daos.  Continuing..."
    fi
fi
x=0
if grep "# DAOS_BASE # added by ftest.sh" /etc/fstab; then
    nfs_mount=true
else
    nfs_mount=false
fi
sudo sed -i -e "/added by ftest.sh/d" /etc/fstab
if [ -n "$DAOS_BASE" ] && $nfs_mount; then
    while [ $x -lt 30 ] &&
          grep "$DAOS_BASE" /proc/mounts &&
          ! sudo umount "$DAOS_BASE"; do
        sleep 1
        (( x+=1 ))
    done
    if grep "$DAOS_BASE" /proc/mounts; then
        echo "Failed to unmount $DAOS_BASE"
        exit 1
    fi
    if [ -d "$DAOS_BASE" ] && ! sudo rmdir "$DAOS_BASE"; then
        echo "Failed to remove $DAOS_BASE"
        if [ -d "$DAOS_BASE" ]; then
            ls -l "$DAOS_BASE"
        else
            echo "because it does not exist"
        fi
        exit 1
    fi
fi
