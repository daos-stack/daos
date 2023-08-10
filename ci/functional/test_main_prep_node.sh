#!/bin/bash

set -eux

: "${FIRST_NODE:=}"
: "${OPERATIONS_EMAIL:=}"

result=0
mail_message=''
nl="
"

function do_mail {
    if [ -z "$mail_message" ]; then
        return
    fi
    set +x
    if [ -z "$OPERATIONS_EMAIL" ]; then
        echo "$mail_message"
        return
    fi
    # shellcheck disable=SC2059
    mail -s "Hardware check failed after reboot!" \
         -r "$HOSTNAME"@intel.com "$OPERATIONS_EMAIL" \
         <<< "$mail_message"
    set -x
}

if ! command -v lspci; then
    if command -v dnf; then
       dnf -y install pciutils
    else
       echo "pciutils not installed, can not test for Infiniband devices"
    fi
fi

hdr_count=0
opa_count=0
ib_count=-1

set +x
while IFS= read -r line; do
    if [[ "$line" != *" Omni-Path "* ]]; then continue; fi
        ((opa_count=opa_count+1))
done < <(lspci | grep "Omni-Path")
if [ $opa_count -gt 0 ]; then
    ((ib_count=opa_count-1)) || true
fi

while IFS= read -r line; do
    if [[ "$line" != *"ConnectX"* ]]; then continue; fi
    mlnx_type="${line##*-}"
    mlnx_type="${mlnx_type%*]}"
    if [ "$mlnx_type" -ge 6 ]; then
        ((hdr_count=hdr_count+1))
    fi
done < <(lspci | grep "ConnectX")
if [ $hdr_count -gt 0 ]; then
    ((ib_count=hdr_count-1)) || true
fi

# Can not have Omni-Path and Mellanox HDR on the same system.
if [ $hdr_count -gt 0 ] && [ $opa_count -gt 0 ]; then
    ib_message="$({
        echo "Invalid hardware configuration.  Found:"
        echo "$hdr_count Mellanox HDR ConnectX adapters."
        echo "and"
        echo "$opa_count Omni-Path adapters"
        echo "The Onmi-Path adapters will not be used."
        })"
    mail_message+="${nl}${ib_message}${nl}"
    echo "$ib_message"
fi
set -x

function do_wait_for_ib {
    ib_timeout=300 # 5 minutes
    retry_wait=10 # seconds
    timeout=$((SECONDS + ib_timeout))
    while [ "$SECONDS" -lt "$timeout" ]; do
      ip link set up "$1" || true
      sleep 2
      if ip addr show "$iface" | grep "inet "; then
        return 0
      fi
      sleep ${retry_wait}
    done
}

# First check for infinband devices
for i in $(seq 0 $ib_count); do
    iface="ib$i"
    if do_wait_for_ib "$iface"; then
        set +x
        if ! ip addr show "$iface" | grep "inet "; then
            ib_message="$({
                echo "Found interface $iface down after reboot on $HOSTNAME"
                ip addr show "$iface" || true
                cat /sys/class/net/"$iface"/mode || true
                ip link set up "$iface" || true
                cat /etc/sysconfig/network-scripts/ifcfg-"$iface" || true
                } 2>&1)"
            mail_message+="${nl}${ib_message}${nl}"
            echo "$ib_message"
        else
            echo "OK: Interface $iface is up."
        fi
        if ! ip addr show "$iface" | grep "inet "; then
            echo "Fail: $ib_message"
            result=1
            echo "Failed to bring up interface $iface on $HOSTNAME. " \
            "Please file a SRE ticket."
        fi
        if [ -e "/sys/class/net/$iface/device/numa_node" ]; then
            set -x
            cat "/sys/class/net/$iface/device/numa_node"
        fi
        set -x
    else
        ib_message="Failed to bring up interface $iface on $HOSTNAME. "
        mail_message+="${nl}${ib_message}${nl}"
        echo "$ib_message"
        result=1
        echo "Please file a SRE ticket."
    fi
done

# having -x just makes the console log harder to read.
set +x
if [ -e /sys/class/net/ib1 ]; then
    # now check for pmem & NVMe drives when ib1 is present.
    # ipmctl show -dimm should show an even number of drives, all healthy
    dimm_count=0
    while IFS= read -r line; do
        if [[ "$line" != *"| Healthy "* ]]; then continue; fi
        ((dimm_count=dimm_count+1))
    done < <(ipmctl show -dimm)
    if [ $dimm_count -eq 0 ] || [ $((dimm_count%2)) -ne 0 ]; then
       dimm_message="FAIL: Wrong number $dimm_count healthy PMEM DIMMs seen."
       mail_message+="$nl$dimm_message$nl$(ipmctl show -dimm)$nl"
    else
       echo "OK: Found $dimm_count PMEM DIMMs."
    fi
    # Should have 2 regions 0x0000 and 0x0001, type AppDirect
    dimm_rcount=0
    while IFS= read -r line; do
        if [[ "$line" != *"| AppDirect"*"| Healthy"* ]]; then continue; fi
        ((dimm_rcount=dimm_rcount+1))
    done < <(ipmctl show -region)

    if [ $dimm_rcount -ne 2 ]; then
       nvme_message="FAIL: Found $dimm_rcount of DIMM PMEM regions, need 2."
       mail_message+="$nl$nvme_message$nl$(ipmctl show -region)$nl"
       result=1
    else
       echo "OK: Found $dimm_rcount DIMM PMEM regions."
    fi

    # While this gets more data than needed, it is the same search that
    # DAOS tests do and records it in the console log.
    nvme_devices="$(lspci -vmm -D | grep -E '^(Slot|Class|Device|NUMANode):' |
                  grep -E 'Class:\s+Non-Volatile memory controller' -B 1 -A 2)"
    nvme_count=0
    while IFS= read -r line; do
        if [[ "$line" != *"Class:"*"Non-Volatile memory controller"* ]];then
            continue
        fi
        nvme_count=$((nvme_count+1))
    done < <(printf %s "$nvme_devices")

    if [ $((nvme_count%2)) -ne 0 ]; then
       nvme_message="Fail: Odd number ($nvme_count) of NVMe devices seen."
       mail_message+="$nl$nvme_message$nl$nvme_devices$nl"
       result=1
    else
       echo "OK: Even number ($nvme_count) of NVMe devices seen."
    fi

    # All storage found by lspci should also be in lsblk report
    lsbk_nvme=0
    lsbk_pmem=0
    while IFS= read -r line; do
        if [[ "$line" = nvme* ]];then
            lsbk_nvme=$((lsbk_nvme+1))
        fi
        if [[ "$line" = pmem* ]];then
            lsbk_pmem=$((lsbk_pmem+1))
        fi
    done < <(lsblk)

    if [ "$lsbk_nvme" -ne "$nvme_count" ]; then
       lsbk_nvme_msg="Fail: Only $lsbk_nvme of $nvme_count NVMe devices seen."
       mail_message+="$nl$lsbk_nvme_msg$nl$(lsblk)$nl"
       result=1
    else
       echo "OK: All $nvme_count NVMe devices are in lsbk report."
    fi
    if [ "$lsbk_pmem" -ne "$dimm_rcount" ]; then
       lsbk_pmem_msg="Only $lsbk_pmem of $dimm_rcount PMEM devices seen."
       mail_message+="$nl$lsbk_pmem_msg$nl$(lsblk)$nl"
       result=1
    else
       echo "OK: All $dimm_rcount PMEM devices are in lsbk report."
    fi
fi

# Additional information if any check failed
if [ "$result" -ne 0 ]; then
    sys_message="$({
        ls -l /etc/sysconfig/network-scripts/ifcfg-* || true
        ip link show|| true
        systemctl status || true
        systemctl --failed || true
        journalctl -n 500 || true
    } 2<&1)"
    mail_message+="$nl$sys_message$nl"
fi

set -x
if [ -n "$FIRST_NODE" ] && ! grep /mnt/share /proc/mounts; then
    mkdir -p /mnt/share
    mount "$FIRST_NODE":/export/share /mnt/share
fi

do_mail

exit $result
