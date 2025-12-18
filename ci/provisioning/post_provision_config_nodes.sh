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
  set +e # for debug purpose
  for addr in $(echo "$nvme_pcie_addrs" | sort); do
    echo "Binding $addr"
    echo "$addr" | sudo tee /sys/bus/pci/drivers/nvme/bind
  done
  set -e
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
    if [ ! -d "${mnt}" ]; then
      sudo mkdir -p "${mnt}"
    fi
    sudo mount "${dev}" "${mnt}"
  done
  SPDK_PCI_ALLOWED=${SPDK_PCI_ALLOWED% }  # remove trailing space
}

function setup_spdk_nvme {
  if [ -d /usr/share/daos/spdk/scripts/ ] && [ -f /usr/share/daos/spdk/scripts/setup.sh ]; then
    export PCI_ALLOWED="$1"
    pushd /usr/share/daos/spdk/scripts/
    set +e
    sudo ./setup.sh
    set -e
    popd
  else
    echo "Required spdk/scripts/setup.sh not found!"
  fi
}

nvme_count=$(nvme_count_devices)
if [ "$nvme_count" -gt 1 ]; then
  ((nvme_count--)) || true
  #nvme_unmount_all $nvme_count
  #nvme_bind_all_in_order
  #nvme_recreate_namespace $nvme_count
  #nvme_reserve_2_disk_per_numa $nvme_count
  #setup_spdk_nvme $SPDK_PCI_ALLOWED

fi

# Workaround to enable binding devices back to nvme or vfio-pci after they are unbound from vfio-pci
# to nvme.  Sometimes the device gets unbound from vfio-pci, but it is not removed the iommu group
# for that device and future bindings to the device do not work, resulting in messages like, "NVMe
# SSD [xxxx:xx:xx.x] not found" when starting daos engines.
if lspci | grep -i nvme; then
  export COVFILE=/tmp/test.cov
  daos_server nvme reset && rmmod vfio_pci && modprobe vfio_pci
fi

function mount_nvme_drive {
    local drive="$1"
    file_system=$(file -sL "/dev/$drive")
    if  [[ "$file_system" != *"ext4 filesystem"* ]]; then
        yes | mkfs -t ext4 "/dev/$drive"
    fi
    mkdir -p "/mnt/$drive"
    mount "/dev/$drive" "/mnt/$drive"
}


nvme_class="/sys/class/nvme/"
function nvme_limit {
    set +x
    if [ ! -d "${nvme_class}" ] || [ -z "$(ls -A "${nvme_class}")" ]; then
        echo "No NVMe devices found"
        return
    fi
    local numa0_devices=()
    local numa1_devices=()
    for nvme_path in "$nvme_class"*; do
        nvme="$(basename "$nvme_path")n1"
        numa_node="$(cat "${nvme_path}/numa_node")"
        if mount | grep "$nvme"; then
            continue
        fi
        if [ "$numa_node" -eq 0 ]; then
            numa0_devices+=("$nvme")
        else
            numa1_devices+=("$nvme")
        fi
    done
    echo numa0 "${numa0_devices[@]}"
    echo numa1 "${numa1_devices[@]}"
    if [ "${#numa0_devices[@]}" -gt 0 ] && [ "${#numa1_devices[@]}" -gt 0 ]; then
        echo "balanced NVMe configuration possible"
        nvme_count=0
        for nvme in "${numa0_devices[@]}"; do
            if [ "$nvme_count" -ge "${DAOS_CI_NVME_NUMA_LIMIT}" ]; then
                mount_nvme_drive "$nvme"
            else
                ((nvme_count++)) || true
            fi
        done
        nvme_count=0
        for nvme in "${numa1_devices[@]}"; do
            if [ "$nvme_count" -ge "${DAOS_CI_NVME_NUMA_LIMIT}" ]; then
                mount_nvme_drive "$nvme"
            else
                ((nvme_count++)) || true
            fi
        done
    else
        echo "balanced NVMe configuration not possible"
        for nvme in "${numa0_devices[@]}" "${numa1_devices[@]}"; do
            ((needed = "$DAOS_CI_NVME_NUMA_LIMIT" + 1)) || true
            nvme_count=0
            if [ "$nvme_count" -ge "$needed" ]; then
                mount_nvme_drive "$nvme"
            else
                ((nvme_count++)) || true
            fi
        done
    fi
    set -x
}

# Force only the desired number of NVMe devices to be seen by DAOS tests
# by mounting the extra ones.
nvme_limit

systemctl enable nfs-server.service
systemctl start nfs-server.service
sync
sync
exit 0
