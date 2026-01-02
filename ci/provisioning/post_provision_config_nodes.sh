#!/bin/bash
#
#  Copyright 2020-2023 Intel Corporation.
#  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -euxo pipefail

env > /root/last_run-env.txt

# Need this fix earlier
# For some reason sssd_common must be reinstalled
# to fix up the restored image.
if command -v dnf; then
    bootstrap_dnf
fi

# If in CI use made up user "Jenkins" with UID that the build agent is
# currently using.   Not sure that the UID is actually important any more
# and that parameter can probably be removed in the future.
# Nothing actually cares what the account name is as long as it does not
# conflict with an existing name and we are consistent in its use.
CI_USER="jenkins"

mkdir -p /localhome
if ! getent passwd "$CI_USER"; then
  # If that UID already exists, then this is not being run in CI.
  if ! getent passwd "$MY_UID"; then
    if ! getent group "$MY_UID"; then
      groupadd -g "$MY_UID" "$CI_USER"
    fi
    useradd -b /localhome -g "$MY_UID" -u "$MY_UID" -s /bin/bash "$CI_USER"
  else
    # Still need a "$CI_USER" account, so just make one up.
    useradd -b /localhome -s /bin/bash "$CI_USER"
  fi
fi
ci_uid="$(id -u $CI_USER)"
ci_gid="$(id -g $CI_USER)"
jenkins_ssh=/localhome/"$CI_USER"/.ssh
mkdir -p "${jenkins_ssh}"
if ! grep -q -s -f /tmp/ci_key.pub "${jenkins_ssh}/authorized_keys"; then
  cat /tmp/ci_key.pub >> "${jenkins_ssh}/authorized_keys"
fi
root_ssh=/root/.ssh
if ! grep -q -f /tmp/ci_key.pub "${root_ssh}/authorized_keys"; then
  cat /tmp/ci_key.pub >> "${root_ssh}/authorized_keys"
fi
cp /tmp/ci_key.pub "${jenkins_ssh}/id_rsa.pub"
cp /tmp/ci_key "${jenkins_ssh}/id_rsa"
cp /tmp/ci_key_ssh_config "${jenkins_ssh}/config"
chmod 700 "${jenkins_ssh}"
chmod 600 "${jenkins_ssh}"/{authorized_keys,id_rsa*,config}
chown -R "${ci_uid}.${ci_gid}" "/localhome/${CI_USER}/"
echo "$CI_USER ALL=(ALL) NOPASSWD: ALL" > "/etc/sudoers.d/$CI_USER"

# DAOS tests need to be changed to use /CIShare instead.
if [ -n "$DAOS_CI_INFO_DIR" ]; then
    mkdir -p /CIShare
    retry_cmd 2400 mount "${DAOS_CI_INFO_DIR}" /CIShare
fi

# defined in ci/functional/post_provision_config_nodes_<distro>.sh
# and catted to the remote node along with this script
if ! post_provision_config_nodes; then
    rc=${PIPESTATUS[0]}
    echo "post_provision_config_nodes failed with rc=$rc"
    exit "$rc"
fi

# Workaround to enable binding devices back to nvme or vfio-pci after they are unbound from vfio-pci
# to nvme.  Sometimes the device gets unbound from vfio-pci, but it is not removed the iommu group
# for that device and future bindings to the device do not work, resulting in messages like, "NVMe
# SSD [xxxx:xx:xx.x] not found" when starting daos engines.
if lspci | grep -i nvme; then
  export COVFILE=/tmp/test.cov
  daos_server nvme reset && rmmod vfio_pci && modprobe vfio_pci
fi

# This workaround ensures that the NVMe configuration remains consistent across
# all cluster nodes.
# This prevents situations where the binding between NVMe devices and PCIe
# addresses varies from restart to restart, resulting in error messages such as
# "Failed to initialize SSD: [xxxx:xx:xx.x]' when DAOS engines are started.
SPDK_SETUP_CMD="/usr/share/daos/spdk/scripts/setup.sh"

check_spdk_setup_cmd () {
  if [ ! -d "$(dirname "$SPDK_SETUP_CMD")" ] || [ ! -f "$SPDK_SETUP_CMD" ]; then
    echo -n "Required SPDK scripts directory $(dirname "$SPDK_SETUP_CMD")"
    echo " or setup.sh not found!"
    return 1
  fi
  return 0
}

get_nvme_count_devices () {
  lspci -D | grep -c -E "Non-Volatile memory controller" || true
}

declare -A MOUNTED_PCI_DEVICES
declare -A PCI_DEVICES_WITH_DATA
pci_device_create_cache () {
  MOUNTED_PCI_DEVICES=()
  PCI_DEVICES_WITH_DATA=()
  if check_spdk_setup_cmd; then
    local status_output line pci_device_address
    status_output="$($SPDK_SETUP_CMD status 2>&1)"
    while read -r line; do
      pci_device_address="${line%% *}"
      if [[ "$pci_device_address" =~ ^[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-9]$ ]]; then
        [[ "$line" == *"Active devices: mount@"* ]] && MOUNTED_PCI_DEVICES["$pci_device_address"]=1
        [[ "$line" == *"Active devices: data@"* ]] && PCI_DEVICES_WITH_DATA["$pci_device_address"]=1
      fi
    done <<< "$status_output"
  fi
  echo "Cached ${#MOUNTED_PCI_DEVICES[@]} mounted PCI devices"
  echo "Cached ${#PCI_DEVICES_WITH_DATA[@]} PCI devices with data"
}

pci_device_is_mounted() {
    local pci_device_address="${1:?Usage: pci_device_is_mounted <pci_device_address>}"
    [[ -v MOUNTED_PCI_DEVICES[$pci_device_address] ]]
}

pci_device_has_data() {
    local pci_device_address="${1:?Usage: pci_device_has_data <pci_device_address>}"
    [[ -v PCI_DEVICES_WITH_DATA[$pci_device_address] ]]
}

pci_device_get_numa () {
  local pci_device="${1:?Usage: pci_device_get_numa <pci_device_address>}"
  local pci_device_numa_path="/sys/bus/pci/devices/${pci_device}/numa_node"
  cat "${pci_device_numa_path}"
}

nvme_dev_get_first_by_pcie_addr (){
  local pci_device_address="${1:?Usage: nvme_dev_get_first_by_pcie_addr <pci_device_address>}"
  local nvme_dir="/sys/bus/pci/devices/$pci_device_address/nvme"
  local nvme_device symlink
  if [ -d "$nvme_dir" ]; then
    for symlink in "$nvme_dir"/*; do
      [ -e "$symlink" ] || continue
      nvme_device=$(basename "$symlink")
      echo -n "${nvme_device}"
      return
    done
  else
    echo "ERROR: nvme_dev_get_first_by_pcie_addr can not find nvme for $pci_device_address"
    exit 1
  fi
}

# Calculates --nsze for a device so the namespace spans the full usable capacity
nvme_calc_full_nsze () {
    local nvme_device="${1:?Usage: nvme_calc_full_nsze_ncap <nvme_device>}"
    # Query the NVMe device info for total logical blocks and LBA size
    # Prefer tnvmcap, fallback to unvmcap if tnvmcap not found
    local nvmcap_bytes
    nvmcap_bytes=$(nvme id-ctrl "$nvme_device" 2>/dev/null | \
        awk -F: '
            /tnvmcap/ {gsub(/[^0-9]/,"",$2); print $2; found=1; exit}
            /unvmcap/ && !found {gsub(/[^0-9]/,"",$2); val=$2}
            END{if(!found && val) print val}
        ')
    
    if [[ -z "$nvmcap_bytes" || "$nvmcap_bytes" -eq 0 ]]; then
        echo "ERROR: Could not find tnvmcap or unvmcap in nvme id-ctrl output" >&2
        return 1
    fi

    # Extract the size of a logical block (lba size), usually from nvme id-ns or id-ctrl
    local lbads="" id_ns="" lba_bytes="" lba_count=""
    id_ns=$(nvme id-ns "${nvme_device}n1" 2>/dev/null || true)
    if [[ -n "$id_ns" ]]; then
        # Look for "lbads" line in id-ns output
        lbads=$(echo "$id_ns" | awk -F: '/lbads/ {gsub(/[^0-9]/,"",$2); print $2; exit}')
    fi
    if [[ -z "$lbads" ]]; then
        # fallback: Try to get LBA (logical block addressing) from id-ctrl if possible, else default to 512
        lbads=12 # Default for 4096 bytes (2^12 = 4096)
    fi
    lba_bytes=$((2 ** lbads))

    # Calculate number of logical blocks
    lba_count=$(( nvmcap_bytes / lba_bytes ))

    # Output as hexadecimal format for nvme-cli
    printf -- "0x%x\n" "$lba_count"
}

nvme_recreate_namespace (){
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

  local nvme_device="${1:?Usage: nvme_recreate_namespace <nvme_device>  [skip_delete:true|false]}"
  local skip_delete="${2:-false}"  # true to skip, default false (delete enabled)
  local nvme_device_path="/dev/${nvme_device}"
  local nvme_device_ns_path="${nvme_device_path}n1"
  local nvme_nsze nvme_cntlid
  # Optionally skip delete step
  if [[ "$skip_delete" != "true" ]]; then
    nvme delete-ns "$nvme_device_path" -n 1 || \
      { echo "ERROR: delete the ${nvme_device_path} namespace failed"; exit 1; }
    nvme reset "$nvme_device_path" || \
      { echo "ERROR: reset the ${nvme_device_path} device failed"; exit 1; }
  else
    echo "INFO: Skipping namespace delete on $nvme_device_path"
  fi
  nvme reset "$nvme_device_path" || \
    { echo "ERROR: reset the ${nvme_device_path} device failed"; exit 1; }
  
  nvme_nsze=$(nvme_calc_full_nsze "${nvme_device_path}")
  nvme create-ns "$nvme_device_path" "--nsze=${nvme_nsze}" "--ncap=${nvme_nsze}" --flbas=0 || \
    { echo "ERROR: create the ${nvme_device_path} namespace failed"; exit 1; }
  nvme_cntlid=$(nvme id-ctrl "$nvme_device_path" | grep -iw cntlid | cut -d: -f2 | tr -d ' ')
  nvme attach-ns "$nvme_device_path" -n 1 -c "$nvme_cntlid" || \
    { echo "ERROR: attach the ${nvme_device_path} namespace failed"; exit 1; }
  # Wait up to 5 seconds for device node to appear
  for i in {1..5}; do
    if [ -b "$nvme_device_ns_path" ]; then
        break
    fi
    sleep "$i"
  done
  if [ ! -b "$nvme_device_ns_path" ]; then
    echo "ERROR: Namespace $nvme_device_ns_path did not appear after attach"
    exit 1
  fi
  # selects LBA format index 0 (512B) and no secure erase, just format
  nvme format "$nvme_device_ns_path" --lbaf=0 --ses=0 --force || \
    { echo "ERROR: format the ${nvme_device_ns_path} namespace failed"; exit 1; }
  nvme reset "$nvme_device_path" || \
    { echo "ERROR: reset the ${nvme_device_path} namespace failed"; exit 1; }
  nvme id-ns "$nvme_device_ns_path" |grep -E "lbaf|nvmcap|nsze|ncap|nuse"
}

# Format ext4 on each element of array after "daos_reserved" is reached.
mkfs_on_nvme_over_limit () {
  local daos_nvme_numa_limit="${1:?Usage: mkfs_on_nvme_over_limit <daos_nvme_numa_limit> <nvme_pci_address_array>}"
  shift
  local nvme_pci_address_array=("$@")
  local count=0
  local nvme_pci_address nvme_device nvme_device_ns_path
  for nvme_pci_address in "${nvme_pci_address_array[@]}"; do
    nvme_device=$(nvme_dev_get_first_by_pcie_addr "$nvme_pci_address")
    nvme_device_ns_path="/dev/${nvme_device}n1"
    # always recreate namespace if it does not exist
    if [ ! -e "$nvme_device_ns_path" ]; then
      echo "INFO recreate namespace 1 on /dev/${nvme_device} ${nvme_pci_address}"
      nvme_recreate_namespace "$nvme_device" true
    fi
    if [ "$count" -ge "$daos_nvme_numa_limit" ]; then
      if ! blkid -t TYPE=ext4 "$nvme_device_ns_path" >/dev/null 2>&1; then
        echo "INFO mkfs on $nvme_device_ns_path"
        sudo mkfs.ext4 -F "$nvme_device_ns_path" > /dev/null
      else
        echo "SKIP mkfs on $nvme_device_ns_path"
      fi
    else
      if pci_device_has_data "$nvme_pci_address"; then
        echo "INFO clean /dev/${nvme_device} ${nvme_pci_address}"
        nvme_recreate_namespace "$nvme_device"
      else
        echo "SKIP clean /dev/${nvme_device} ${nvme_pci_address}"
      fi
    fi
    ((count++)) || true
  done
}

nvme_setup (){
  local daos_nvme_numa_limit="${1:-?Usage: nvme_setup <daos_nvme_numa_limit>}"
  local numa0_pci_devices=()
  local numa1_pci_devices=()
  local all_numas_pci_devices
  local nvme_count nvme_pcie_address_all nvme_pci_address numa_node  
  
  nvme_count=$(get_nvme_count_devices)
  if [ "$nvme_count" -le 1 ]; then # Expect at least 2 NVMe devices for proper setup
    return 0
  fi
  
  if ! check_spdk_setup_cmd; then
    exit 1
  fi

  set +x
  pci_device_create_cache
  set -x

  nvme_pcie_address_all=$(lspci -D | awk '/Non-Volatile memory controller/{print $1}' | sort)

  for nvme_pci_address in $nvme_pcie_address_all; do
    # Skip already mounted namespace
    if pci_device_is_mounted "$nvme_pci_address"; then
      echo "Skip already mounted namespace $nvme_pci_address"
      continue
    fi
    numa_node="$(pci_device_get_numa "$nvme_pci_address")"
    if [ "$numa_node" -eq 0 ]; then
      numa0_pci_devices+=("$nvme_pci_address")
    else
      numa1_pci_devices+=("$nvme_pci_address")
    fi
  done
  echo NUMA0 PCIe devices: "${numa0_pci_devices[@]}"
  echo NUMA1 PCIe devices: "${numa1_pci_devices[@]}"
  if [ "${#numa0_pci_devices[@]}" -ge "$daos_nvme_numa_limit" ] && \
    [ "${#numa1_pci_devices[@]}" -ge "$daos_nvme_numa_limit" ]; then
    echo "balanced NVMe configuration possible"
    mkfs_on_nvme_over_limit "$daos_nvme_numa_limit" "${numa0_pci_devices[@]}"
    mkfs_on_nvme_over_limit "$daos_nvme_numa_limit" "${numa1_pci_devices[@]}"
  else
    daos_nvme_numa_limit=$((daos_nvme_numa_limit + daos_nvme_numa_limit))
    all_numas_pci_devices=( "${numa0_pci_devices[@]}" "${numa1_pci_devices[@]}" )
    echo "balanced NVMe configuration not possible"
    mkfs_on_nvme_over_limit "$daos_nvme_numa_limit" "${all_numas_pci_devices[@]}"
  fi
}

function spdk_setup_status {
    set +e
    if check_spdk_setup_cmd; then
       "$SPDK_SETUP_CMD" status
    fi
    set -e
}

#For now limit to 2 devices per CPU NUMA node
: "${DAOS_CI_NVME_NUMA_LIMIT:=2}"

spdk_setup_status
nvme_setup "$DAOS_CI_NVME_NUMA_LIMIT"
spdk_setup_status
if command -v daos_server >/dev/null 2>&1; then
    daos_server nvme scan
fi

systemctl enable nfs-server.service
systemctl start nfs-server.service
sync
sync
exit 0
