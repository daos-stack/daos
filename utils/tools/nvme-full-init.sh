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
#set -e
#set -x
set -eu

NVME_MAX_GLOBAL=15
function nvme_count_devices {
  local count
  count=$(lspci -D | grep -E 'Non-Volatile memory controller' | wc -l)
  echo $count
}

function nvme_unmount_all {
  local NVME_MAX=${1:-$NVME_MAX_GLOBAL}
  echo "Unmounting all /dev/nvme[0-${NVME_MAX}]n1 mountpoints (if any)..."
  set +e # we can try to unmount unexisted disks
  for i in $(seq 0 $NVME_MAX); do
    nvme="nvme${i}n1"
    dev="/dev/${nvme}"
    mnt="/mnt/${nvme}"
    if [ -b "$dev" ]; then
      # Get mountpoint (empty if not mounted)
      mp=$(lsblk -nr -o MOUNTPOINT "$dev")
      if [ -n "$mp" ]; then
        echo "Unmounting $dev from $mp"
        sudo umount -f "$dev" 2>/dev/null || sudo umount -f "$mp" 2>/dev/null
      else
        echo "Unmounting ${mnt}"
        sudo umount -f "${mnt}" 2>/dev/null
      fi
    elif [ -d "${mnt}" ]; then
      echo "Force umount of ${mnt}"
      sudo umount -f "${mnt}" 2>/dev/null
    fi
  done
  set -e
}

function nvme_bind_all_in_order {
  # Find all PCI addresses for NVMe controllers
  local nvme_pcie_addrs
  nvme_pcie_addrs=$(lspci -D | awk '/Non-Volatile memory controller/{print $1}')

  if [ -z "$nvme_pcie_addrs" ]; then
    echo "No NVMe PCI devices found."
    return 0
  fi

  #echo "Found PCI NVMe addresses:"
  #echo "$nvme_pcie_addrs"
  #echo

  for dir in /sys/class/nvme/*/; do
    echo "$dir: $(ls -la ${dir} | grep device | awk -F'-> ' '{print $2}' | sed 's|.*/||')"
  done

  # Unbind all NVMe devices
  echo "Unbinding NVMe devices from nvme driver (or vfio-pci driver) ..."
  set +e # it's ok if a device isn't bound to one of the drivers
  for addr in $nvme_pcie_addrs; do
    echo "Unbinding $addr"
    if [ -f "/sys/bus/pci/drivers/nvme/${addr}" ]; then
        echo "$addr" | sudo tee /sys/bus/pci/drivers/nvme/unbind > /dev/null 2>&1
    fi
    if [ -f "/sys/bus/pci/drivers/vfio-pci/${addr}" ]; then
        echo "$addr" | sudo tee /sys/bus/pci/drivers/vfio-pci/unbind > /dev/null 2>&1
    fi
  done
  set -e

  echo
  echo "Binding NVMe devices to nvme driver in sorted order..."
  for addr in $(echo "$nvme_pcie_addrs" | sort); do
    echo "Binding $addr"
    echo "$addr" | sudo tee /sys/bus/pci/drivers/nvme/bind > /dev/null 2>&1
  done
}


function nvme_recreate_namespace {
  set +e
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

  local NVME_MAX=${1:-$NVME_MAX_GLOBAL}
  for i in $(seq 0 $NVME_MAX); do
    dev="/dev/nvme${i}"
    dev_ns="${dev}n1"
    echo "Recreating namespace on $dev ..."
    nvme delete-ns $dev -n 0x1
    nvme reset $dev
    nvme create-ns $dev --nsze=0x1bf1f72b0 --ncap=0x1bf1f72b0 --flbas=0
    nvme attach-ns $dev -n 0x1 -c 0x41
    # selects LBA format index 0 (512BK) and no secure erase, just format.
    nvme format $dev_ns --lbaf=0 --ses=0 --force
    nvme reset $dev
    nvme id-ns $dev_ns |grep -E "lbaf|nvmcap|nsze|ncap|nuse"
  done
  set -e
}

SPDK_PCI_ALLOWED=""
function nvme_reserve_2_disk_per_numa {
  local NVME_MAX=${1:-$NVME_MAX_GLOBAL}
  local numa_node_0=0
  local numa_node_1=0
  for i in $(seq 0 $NVME_MAX); do
    nvme="nvme${i}"
    numa_path="/sys/class/nvme/${nvme}/numa_node"
    dev="/dev/${nvme}n1"
    mnt="/mnt/${nvme}n1"
    numa_node="$(cat "$numa_path")"
    pci_addr=$(basename "$(readlink -f /sys/class/nvme/${nvme}/device)")
    echo "$nvme: NUMA node $numa_node, PCI addr $pci_addr, numa_node_0 $numa_node_0, numa_node_1 $numa_node_1"
    if [ "$numa_node" -eq 0 ]; then
      ((numa_node_0++)) || true
      if [ "$numa_node_0" -lt 3 ]; then
        SPDK_PCI_ALLOWED="$SPDK_PCI_ALLOWED$pci_addr "
        echo "NUMA0: ${nvme} -> ${pci_addr}"
        continue
      fi
    else
      ((numa_node_1++)) || true
      if [ "$numa_node_1" -lt 3 ]; then
        SPDK_PCI_ALLOWED="$SPDK_PCI_ALLOWED$pci_addr "
        echo "NUMA1: ${nvme} -> ${pci_addr}"
        continue
      fi
    fi
    sudo mkfs.ext4 -F "${dev}"
    if [ ! -d "${mnt}" ]; then
      sudo mkdir -p "${mnt}"
    fi
    sudo mount "${dev}" "${mnt}"
  done
  SPDK_PCI_ALLOWED=${SPDK_PCI_ALLOWED% }  # remove trailing space
}

nvme_count=$(nvme_count_devices)
if [ "$nvme_count" -gt 1 ]; then
  ((nvme_count--)) || true
  nvme_unmount_all $nvme_count
  nvme_bind_all_in_order $nvme_count
  nvme_recreate_namespace $nvme_count
  nvme_reserve_2_disk_per_numa $nvme_count

  #echo "Done. All NVMe devices have been re-bound in sorted PCI address order."
  #for i in $(seq 0 $NVME_MAX_GLOBAL); do
  #  echo -n "nvme$i: "
  #	cat /sys/class/nvme/nvme$i/address 2>/dev/null || echo "not found"
  #done

  pushd /usr/share/daos/spdk/scripts/
  export PCI_ALLOWED="$SPDK_PCI_ALLOWED"
  sudo ./setup.sh

  daos_server nvme scan
fi