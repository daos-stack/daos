#!/bin/bash
# Copyright (C) Copyright 2020 Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted for any purpose (including commercial purposes)
# provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions, and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions, and the following disclaimer in the
#    documentation and/or materials provided with the distribution.
#
# 3. In addition, redistributions of modified forms of the source or binary
#    code must carry prominent notices stating that the original code was
#    changed and the date of the change.
#
#  4. All publications or advertising materials mentioning features or use of
#     this software are asked, but not required, to acknowledge that it was
#     developed by Intel Corporation and credit the contributors.
#
# 5. Neither the name of Intel Corporation, nor the name of any Contributor
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
