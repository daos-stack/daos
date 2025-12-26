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
set -eux

SPDK_SETUP_CMD="/usr/share/daos/spdk/scripts/setup.sh"

if [ ! -d $(dirname "$SPDK_SETUP_CMD") ] || [ ! -f "$SPDK_SETUP_CMD" ]; then
    echo "Required scripts directory or setup.sh not found!"
    exit 1
fi

function get_nvme_count_devices {
  local nvme_count=$(lspci -D | grep -E 'Non-Volatile memory controller' | wc -l)
  echo $nvme_count
}

function pci_dev_is_mounted {
  local pci_dev="${1:?Usage: pci_dev_is_mounted <pci_device_address>}"
  $SPDK_SETUP_CMD setup | grep "$pci_dev" | grep "mount@" > /dev/null
}

function pci_dev_has_filesystem {
  local pci_dev="${1:?Usage: pci_dev_has_filesystem <pci_device_address>}"
  $SPDK_SETUP_CMD setup | grep "$pci_dev" | grep "data@" > /dev/null
}

function nvme_dev_is_mounted {
  local nvme_dev="${1:-?Usage: nvme_dev_is_mounted <nvme_device>}"
  $SPDK_SETUP_CMD setup | grep "$nvme_dev" | grep "mount@" > /dev/null
}

function nvme_dev_has_data {
  local nvme_dev="${1:-?Usage: nvme_dev_has_data <nvme_device>}"
  $SPDK_SETUP_CMD setup | grep "data@${nvme_dev}" > /dev/null
}

function pci_dev_get_numa {
  local pci_dev="${1:?Usage: pci_dev_get_numa <pci_device_address>}"
  local pci_dev_numa_path="/sys/bus/pci/devices/${pci_dev}/numa_node"
  cat "${pci_dev_numa_path}"
}

function nvme_dev_get_first_by_pcie_addr() {
  local pci_dev="${1:?Usage: nvme_dev_get_first_by_pcie_addr <pci_device_address>}"
  local nvme_dir="/sys/bus/pci/devices/$pci_dev/nvme"
  if [ -d "$nvme_dir" ]; then
    for symlink in "$nvme_dir"/*; do
      [ -e "$symlink" ] || continue
      local nvme_dev=$(basename "$symlink")
      echo -n "${nvme_dev}"
      return
    done
  else
    echo "ERROR nvme_dev_get_first_by_pcie_addr can not find nvme for $pci_dev"
    exit 1
  fi
}

function nvme_recreate_namespace {
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

  local nvme_dev="${1:?Usage: nvme_dev_get_first_by_pcie_addr <pci_device_address>}"
  local nvme_dev_path="/dev/${nvme_dev}"
  local nvme_dev_ns_path="${nvme_dev_path}n1"
  echo "Recreating namespace on $nvme_dev_path ..."
  nvme delete-ns $nvme_dev_path -n 0x1
  nvme reset $nvme_dev_path
  nvme create-ns $nvme_dev_path --nsze=0x1bf1f72b0 --ncap=0x1bf1f72b0 --flbas=0
  nvme attach-ns $nvme_dev_path -n 0x1 -c 0x41
  # selects LBA format index 0 (512BK) and no secure erase, just format
  nvme format $nvme_dev_ns_path --lbaf=0 --ses=0 --force
  nvme reset $nvme_dev_path
  #nvme id-ns $nvme_dev_ns_path |grep -E "lbaf|nvmcap|nsze|ncap|nuse"
  echo "${nvme_dev_ns_path} done"
}

# Format ext4 on each element of array after "limit" is reached.
mkfs_on_nvme_over_limit() {
  local devices=("${!1}")  # array is passed as a name, bash uses indirection via name[@]
  local limit="$2"
  local count=0
  local nvme_dev_ns
  for nvme_dev in "${devices[@]}"; do
    if [ "$count" -ge "$limit" ]; then
      nvme_dev_ns_path="/dev/${nvme_dev}n1"
      if [ ! -e $nvme_dev_ns_path ]; then
        echo "INFO recreate namespace 1 on /dev/$nvme_dev"
        nvme_recreate_namespace "$nvme_dev"
      fi
      if ! blkid -t TYPE=ext4 "$nvme_dev_ns_path" >/dev/null 2>&1; then 
        echo "INFO mkfs on $nvme_dev_ns_path"
        sudo mkfs.ext4 -F "$nvme_dev_ns_path" > /dev/null
      else
        echo "SKIP mkfs on $nvme_dev_ns_path"
      fi
    else
      if nvme_dev_has_data "$nvme_dev"; then
        echo "INFO clean /dev/${nvme_dev}"
        nvme_recreate_namespace "$nvme_dev"
      else
        echo "SKIP clean /dev/${nvme_dev}"
      fi
    fi
    ((count++)) || true
  done
}

function nvme_setup {
  local nvme_per_numa="${1:-?Usage: nvme_setup <nvme_per_numa_limit>}"
  local nvme_count=$(get_nvme_count_devices)
  if [ "$nvme_count" -gt 1 ]; then
    ((nvme_count--)) || true
  else
    return 0
  fi
  local nvme_dev nvme_pci_address numa_node
  local numa0_devices=()
  local numa1_devices=()
  local nvme_pcie_address_all=$(lspci -D | \
    awk '/Non-Volatile memory controller/{print $1}' | sort)

  for nvme_pci_address in $nvme_pcie_address_all; do
    # Skip already mounted namespace
    if pci_dev_is_mounted $nvme_pci_address; then
      echo "Skip already mounted namespace $nvme_pci_address"
      continue
    fi
    #echo "Binding $nvme_pci_address"
    #echo "$nvme_pci_address" | sudo tee /sys/bus/pci/drivers/nvme/bind
    nvme_dev=$(nvme_dev_get_first_by_pcie_addr "$nvme_pci_address")
    numa_node="$(pci_dev_get_numa "$nvme_pci_address")"
    if [ "$numa_node" -eq 0 ]; then
      numa0_devices+=("$nvme_dev")
    else
      numa1_devices+=("$nvme_dev")
    fi
  done
  echo NUMA0 PCIe devices: "${numa0_devices[@]}"
  echo NUMA1 PCIe devices: "${numa1_devices[@]}"
  if [ "${#numa0_devices[@]}" -ge "$nvme_per_numa" ] && \
    [ "${#numa1_devices[@]}" -ge "$nvme_per_numa" ]; then
    echo "balanced NVMe configuration possible"
    mkfs_on_nvme_over_limit numa0_devices[@] "$nvme_per_numa"
    mkfs_on_nvme_over_limit numa1_devices[@] "$nvme_per_numa"
  else
    echo "balanced NVMe configuration not possible"
    nvme_per_numa=$((nvme_per_numa + nvme_per_numa))
    combined=( "${numa0_devices[@]}" "${numa1_devices[@]}" )
    mkfs_on_nvme_over_limit combined[@] "$nvme_per_numa"
  fi
}

function setup_spdk_nvme {
  if [ -d /usr/share/daos/spdk/scripts/ ] && [ -f /usr/share/daos/spdk/scripts/setup.sh ]; then
    pushd /usr/share/daos/spdk/scripts/
    set +e
    sudo ./setup.sh status
    set -e
    popd
  else
    echo "Required spdk/scripts/setup.sh not found!"
  fi
}

#FOR now limit to 2 devices per CPU NUMA node
: "${DAOS_CI_NVME_NUMA_LIMIT:=2}"

nvme_setup "$DAOS_CI_NVME_NUMA_LIMIT"
setup_spdk_nvme
if command -v daos_server >/dev/null 2>&1; then
    daos_server nvme scan
fi
