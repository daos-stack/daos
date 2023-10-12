#!/bin/bash

# set -x
set -u -e -o pipefail

path=${1:?Missing NVMe device path}

if [[ $(id -u) -ne 0 ]] ; then
	echo "[ERROR] Could only be used by root"
	exit 1
fi

if [[ ! -b "$path" ]] ; then
	echo "[ERROR] Input path '$1' is not a block device"
	exit 1
fi

if mountpoint -q /var/daos/control_meta ; then
	echo "[INFO] Umounting '/var/daos/control_meta'"
	if ! umount /var/daos/control_meta ; then
		echo "[ERROR] Not able to umount '/var/daos/control_meta'"
		exit 1
	fi
fi

if blkid $path &> /dev/null ; then
	echo "[INFO] Removing old file system"
	wipefs -a "$path"
fi

echo "[INFO] Creating new Ext4 file system"
mkfs.ext4 "$path"

echo  "[INFO] Creating new DAOS control metadata mount point"
uuid=$( blkid $path | awk -e '{ print $2 }' | tr -d '"' )
sed -i -E -e '/^UUID=[^[:blank:]]+[[:blank:]]+\/var\/daos\/control_meta[[:blank:]]+ext4.+$/d' /etc/fstab
cat >> /etc/fstab <<EOF
$uuid /var/daos/control_meta ext4 auto,defaults,lazytime,discard 0 0
EOF
grep -e "/var/daos/control_meta" /etc/fstab

echo "[INFO] Mounting DAOS control metadata mount point"
systemctl daemon-reload
if ! mount /var/daos/control_meta ; then
	echo "[ERROR] Not able to mount '/var/daos/control_meta'"
fi
