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
    # This part only until DAOS is migrated to use /CIShare
    rm -f /CIShare
    ln -sfn /CIShare
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

# FOR now limit to 2 devices per CPU NUMA node
: "${DAOS_CI_NVME_NUMA_LIMIT:=2}"

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
    if [ ! -d /sys/class/nvme ]; then
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
