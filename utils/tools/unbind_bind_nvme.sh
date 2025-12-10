#!/bin/bash
#
# Copyright 2025 Hewlett Packard Enterprise Development LP
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Full reinitialization of all NVMe devices on the DAOS cluster node
# - unmount all NVMe devices
# - unbind all NVMe devices and re-bind them to the nvme driver in sorted PCI address order.
# - create new logical block device (namespace) on each NVMe drive
# - mount all except two logical devices to build configuration required for DAOS cluster node
# - initialize two logical devices to work with SPDK (via vfio-pci driver)
# - verify configuration using the `daos_server nvme scan` command.
#
# set -e
# set -x
set -u

echo "Unmounting all /dev/nvme[0-15]n1 mountpoints (if any)..."

for nvme in nvme{0..15}n1; do
  dev="/dev/${nvme}"
  mnt="/mnt/${nvme}"
  if [ -b "$dev" ]; then
    # Get mountpoint (empty if not mounted)
    mp=$(lsblk -nr -o MOUNTPOINT "$dev")
    if [ -n "$mp" ]; then
      echo "Unmounting $dev from $mp"
      sudo umount -f "$dev" || sudo umount -f "$mp"
    else
      echo "Umounting ${mnt}"
      sudo umount -f "${mnt}"
    fi
  fi
  if [ -d "${mnt}" ]; then
    echo "Force umount of ${mnt}"
    sudo umount -f "${mnt}"
  fi
done

# Find all PCI addresses for NVMe controllers
nvme_pcie_addrs=$(lspci -D | awk '/Non-Volatile memory controller/{print $1}')

if [ -z "$nvme_pcie_addrs" ]; then
  echo "No NVMe PCI devices found."
  exit 1
fi

echo "Found PCI NVMe addresses:"
echo "$nvme_pcie_addrs"
echo

# Unbind all NVMe devices
echo "Unbinding NVMe devices from nvme driver (or vfio-pci driver) ..."
for addr in $nvme_pcie_addrs; do
  echo "Unbinding $addr"
  echo "$addr" | sudo tee /sys/bus/pci/drivers/nvme/unbind
  echo "$addr" | sudo tee /sys/bus/pci/drivers/vfio-pci/unbind
done

echo
echo "Binding NVMe devices to nvme driver in sorted order..."
for addr in $(echo "$nvme_pcie_addrs" | sort); do
  echo "Binding $addr"
  echo "$addr" | sudo tee /sys/bus/pci/drivers/nvme/bind
done

# lbaf 0 : ms:0   lbads:9  rp:0x1 (in use)   → 512B blocks
# lbaf 1 : ms:0   lbads:12 rp:0              → 4096B blocks (4K)
# lbaf 2 : ms:8   lbads:9  rp:0x3            → 512B + 8B metadata
# lbaf 3 : ms:8   lbads:12 rp:0x2            → 4K + 8B metadata
# lbaf 4 : ms:64  lbads:12 rp:0x3            → 4K + 64B metadata
# lbads = log2(block size).
# 9 → 2⁹ = 512 bytes
# 12 → 2¹² = 4096 bytes (4K)
# ms = metadata size per block (0, 8, or 64 bytes).
# rp = relative performance hint.

for nvme in {0..15}; do
        dev="/dev/nvme${nvme}"
        dev_ns="${dev}n1"
        nvme delete-ns $dev -n 0x1
        nvme reset $dev
        nvme create-ns $dev --nsze=0x1bf1f72b0 --ncap=0x1bf1f72b0 --flbas=0
        # selects LBA format index 1 (4K) and no secure erase, just format.
        nvme attach-ns $dev -n 0x1 -c 0x41
        nvme format $dev_ns --lbaf=1 --ses=0 --force
        nvme reset $dev
        nvme id-ns $dev_ns
done

for nvme in nvme{2..15}n1; do
  dev="/dev/${nvme}"
  mnt="/mnt/${nvme}"
  mkfs.ext4 "${dev}"
  if [ ! -d "${mnt}" ]; then
    mkdir "${mnt}"
  fi
  sudo mount "${dev}" "${mnt}"
done

echo "Done. All NVMe devices have been re-bound in sorted PCI address order."
for i in $(seq 0 15); do
  echo -n "nvme$i: "
	cat /sys/class/nvme/nvme$i/address 2>/dev/null || echo "not found"
done

pushd /usr/share/daos/spdk/scripts/
export PCI_ALLOWED="0000:2b:00.0 0000:2c:00.0"
sudo ./setup.sh

daos_server nvme scan
