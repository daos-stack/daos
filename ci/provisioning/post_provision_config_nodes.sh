#!/bin/bash
#
#  Copyright 2020-2023 Intel Corporation.
#  Copyright 2025 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -eux

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

# This workaround ensures that the NVMe configuration remains consistent across
# all cluster nodes.
# This prevents situations where the binding between NVMe devices and PCIe
# addresses varies from restart to restart, resulting in error messages such as
# "Failed to initialize SSD: [xxxx:xx:xx.x]' when DAOS engines are started.

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
    set +e
    sudo "$SPDK_SETUP_CMD" status
    set -e
}

# Workaround to enable binding devices back to nvme or vfio-pci after they are unbound from vfio-pci
# to nvme.  Sometimes the device gets unbound from vfio-pci, but it is not removed the iommu group
# for that device and future bindings to the device do not work, resulting in messages like, "NVMe
# SSD [xxxx:xx:xx.x] not found" when starting daos engines.
if lspci | grep -i nvme; then
  export COVFILE=/tmp/test.cov
  daos_server nvme reset && rmmod vfio_pci && modprobe vfio_pci
fi

#FOR now limit to 2 devices per CPU NUMA node
: "${DAOS_CI_NVME_NUMA_LIMIT:=2}"

nvme_setup "$DAOS_CI_NVME_NUMA_LIMIT"
setup_spdk_nvme
if command -v daos_server >/dev/null 2>&1; then
    daos_server nvme scan
fi

systemctl enable nfs-server.service
systemctl start nfs-server.service
sync
sync
exit 0
