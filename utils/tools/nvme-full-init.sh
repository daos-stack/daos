#!/bin/bash
#
#  Copyright 2025 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
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
  echo "Unmounting all /dev/nvme[0-${NVME_MAX}]n1 mountpoints."
  set +e # we can try to unmount unexisted disks
  for i in $(seq 0 $NVME_MAX); do
    nvme="nvme${i}n1"
    dev="/dev/${nvme}"
    mnt="/mnt/${nvme}"
    if [ -b "$dev" ]; then
      mp=$(lsblk -nr -o MOUNTPOINT "$dev")
      if [ -n "$mp" ]; then
        echo "Unmounting $dev from $mp"
        sudo umount -f "$dev"|| sudo umount -f "$mp"
      elif [ -d "${mnt}" ]; then
        echo "Unmounting ${mnt}"
        sudo umount -f "${mnt}"
      fi
    elif [ -d "${mnt}" ]; then
      echo "Force umount of ${mnt}"
      sudo umount -f "${mnt}"
    fi
    rm -rf $mnt
  done
  set -e
}

function nvme_recreate_namespace {
  #set +e
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

  local nvme_idx=${1:-0}
  dev="/dev/nvme${nvme_idx}"
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
  #set -e
  echo "${dev_ns} done"
}

function nvme_bind_all_in_order {
  # Find all PCI addresses for NVMe controllers
  local nvme_pcie_addrs
  nvme_pcie_addrs=$(lspci -D | awk '/Non-Volatile memory controller/{print $1}')

  if [ -z "$nvme_pcie_addrs" ]; then
    echo "No NVMe PCI devices found."
    return 1
  fi

  #echo "Found PCI NVMe addresses:"
  #echo "$nvme_pcie_addrs"
  #echo

  for dir in /sys/class/nvme/*/; do
    numa=$(cat ${dir}numa_node)
    echo "$numa $dir: $(ls -la ${dir} | grep device | awk -F'-> ' '{print $2}' | sed 's|.*/||')"
  done

  # Unbind all NVMe devices
  echo "Unbinding NVMe devices from nvme driver (or vfio-pci driver) ..."
  set +e # it's ok if a device isn't bound to one of the drivers
  for addr in $nvme_pcie_addrs; do
    if [ -e "/sys/bus/pci/drivers/nvme/${addr}" ]; then
      echo "Unbinding $addr from nvme"
      echo "$addr" | sudo tee /sys/bus/pci/drivers/nvme/unbind
    fi
    if [ -e "/sys/bus/pci/drivers/vfio-pci/${addr}" ]; then
      echo "Unbinding $addr from vfio-pci"
      echo "$addr" | sudo tee /sys/bus/pci/drivers/vfio-pci/unbind
    fi
  done
  set -e

  echo
  # Bind all NVMe devices in order
  echo "Binding NVMe devices to nvme driver in sorted order..."
  #set +e # for debug purpose
  local nvme_idx=0
  echo "$nvme_pcie_addrs" | sort | while read -r addr; do
    echo "Binding $addr"
    if((nvme_idx < 2)); then
      echo "$addr" | sudo tee /sys/bus/pci/drivers/nvme/bind
#      sleep 1
      nvme_recreate_namespace $nvme_idx
      sleep 1
    else
      ( echo "$addr" | sudo tee /sys/bus/pci/drivers/nvme/bind ) &
    fi
    echo "${addr} done"
    echo -n "${nvme_idx}"
    ((nvme_idx++))
    echo " -> ${nvme_idx}"
  done
  wait
  #set -e
}


# FOR now limit to 2 devices per CPU NUMA node
: "${DAOS_CI_NVME_NUMA_LIMIT:=2}"

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
      if [ "$numa_node_0" -le $DAOS_CI_NVME_NUMA_LIMIT ]; then
        SPDK_PCI_ALLOWED="$SPDK_PCI_ALLOWED$pci_addr "
        echo "NUMA0: ${nvme} -> ${pci_addr}"
        continue
      fi
    else
      ((numa_node_1++)) || true
      if [ "$numa_node_1" -le $DAOS_CI_NVME_NUMA_LIMIT ]; then
        SPDK_PCI_ALLOWED="$SPDK_PCI_ALLOWED$pci_addr "
        echo "NUMA1: ${nvme} -> ${pci_addr}"
        continue
      fi
    fi
    sudo mkfs.ext4 -F "${dev}"
#    if [ ! -d "${mnt}" ]; then
#      sudo mkdir -p "${mnt}"
#    fi
#    sudo mount "${dev}" "${mnt}"
  done
  SPDK_PCI_ALLOWED=${SPDK_PCI_ALLOWED% }  # remove trailing space
}

function setup_spdk_nvme {
  if [ -d /usr/share/daos/spdk/scripts/ ] && [ -f /usr/share/daos/spdk/scripts/setup.sh ]; then
    export PCI_ALLOWED="${1:- }"
    pushd /usr/share/daos/spdk/scripts/
    set +e
    sudo ./setup.sh status
    set -e
    popd
  else
    echo "Required spdk/scripts/setup.sh not found!"
  fi
}

nvme_count=$(nvme_count_devices)
if [ "$nvme_count" -gt 1 ]; then
  ((nvme_count--)) || true
#  nvme_unmount_all $nvme_count
  nvme_bind_all_in_order
  #nvme_recreate_namespace $nvme_count
  #nvme_reserve_2_disk_per_numa $nvme_count
  setup_spdk_nvme $SPDK_PCI_ALLOWED

  #echo "Done. All NVMe devices have been re-bound in sorted PCI address order."
  #for i in $(seq 0 $NVME_MAX_GLOBAL); do
  #  echo -n "nvme$i: "
  #	cat /sys/class/nvme/nvme$i/address 2>/dev/null || echo "not found"
  #done
  
  #daos_server nvme scan
fi
