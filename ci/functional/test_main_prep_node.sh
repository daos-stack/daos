#!/bin/bash
#
#  Copyright 2020-2023 Intel Corporation.
#  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -eux

: "${FIRST_NODE:=}"
: "${OPERATIONS_EMAIL:=}"
: "${STAGE_NAME:=Unknown}"
: "${BUILD_URL:=Unknown}"
: "${JENKINS_URL:=https://jenkins.example.com}"
domain1="${JENKINS_URL#https://}"
mail_domain="${domain1%%/*}"
: "${EMAIL_DOMAIN:=$mail_domain}"
: "${DAOS_DEVOPS_EMAIL:="$HOSTNAME"@"$EMAIL_DOMAIN"}"
: "${DAOS_INFINIBAND:=}"
: "${DAOS_PMEM:=0}"
: "${DAOS_NVME:=0}"

#cn is for a cleaned up stage name.
cn=$(echo "$STAGE_NAME" | sed 's/[^a-zA-Z0-9_]/_/g' | sed 's/__*/_/g')

result=0
mail_message=''
mail_type='warning'
nl="
"

testcases=''
testruns=0
testfails=0
myhost="${HOSTNAME%%.*}"
: "${NODELIST:=$myhost}"
mynodenum=0

# in order for junit test names to be consistent between test runs
# Need to use the position number of the host in the node list for
# the junit report.
for node in ${NODELIST//,/ }; do
    ((mynodenum++)) || true
    if [ "$node" == "$myhost" ]; then break; fi
done

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
    build_info="BUILD_URL = $BUILD_URL$nl STAGE = $STAGE_NAME$nl$nl"
    mail -s "Hardware check $mail_type after reboot!" \
         -r "$DAOS_DEVOPS_EMAIL" "$OPERATIONS_EMAIL" \
         <<< "$build_info$mail_message"
    set -x
}

if ! command -v lspci; then
    if command -v dnf; then
       dnf -y install pciutils
    else
       echo "pciutils not installed, can not test for hardware devices"
    fi
fi

hdr_count=0
opa_count=0
ib_count=-1

set +x
while IFS= read -r line; do
    ((opa_count++)) || true
done < <(lspci -mm | grep "Omni-Path")
echo "Found $opa_count Omni-Path adapters."
if [ "$opa_count" -gt 0 ]; then
    ((ib_count=opa_count)) || true
fi

last_pci_bus=''
while IFS= read -r line; do
    pci_bus="${line%.*}"
    if [ "$pci_bus" == "$last_pci_bus" ]; then
        # We only use one interface on a dual interface HBA
        # Fortunately lspci appears to group them together
        continue
    fi
    last_pci_bus="$pci_bus"
    mlnx_type="${line##*ConnectX-}"
    mlnx_type="${mlnx_type%]*}"
    if [ "$mlnx_type" -ge 6 ]; then
        ((hdr_count++)) || true
    fi
done < <(lspci -mm | grep "ConnectX" | grep -i "infiniband" )
echo "Found $hdr_count Mellanox HDR adapters."
if [ "$hdr_count" -gt 0 ]; then
    ((ib_count=hdr_count)) || true
fi

# Can not have Omni-Path and Mellanox HDR on the same system.
# Non fatal, just notify e-mail.
if [ "$hdr_count" -gt 0 ] && [ "$opa_count" -gt 0 ]; then
    ib_message="Invalid hardware configuration.  Found:
$hdr_count Mellanox HDR ConnectX adapters,
and
$opa_count Omni-Path adapters.
The Omni-Path adapters will not be used."
    mail_message+="${nl}${ib_message}${nl}"
    echo "$ib_message"
fi
if [ -z "$DAOS_INFINIBAND" ]; then
    DAOS_INFINIBAND=$ib_count
fi
set -x

# Wait for at least the expected IB devices to show up.
# in the case of dual port HBAs, only the ports that are connected may show up.
# For some unknown reason, sometimes IB devices will not show up
# except in the lspci output unless an ip link set up command for
# at least one device that should be present shows up.
good_ibs=()
function do_wait_for_ib {
    # The problem is that we do not know the actual device names
    # ahead of time.  So we try to bring up all possible devices
    # and see if at least the expected number show up with IP
    # addresses.
    local ib_devs=("ib0" "ib1" "ib2" "ib3" "ib4")
          # Udev rule convention, first digit is the numa node
          # second digit should be an index of the HBA on that numa node.
          ib_devs+=("ib_00" "ib_01" "ib_02" "ib_03")
          ib_devs+=("ib_10" "ib_11" "ib_12" "ib_13")
    local working_ib
    ib_timeout=300 # 5 minutes
    retry_wait=10 # seconds
    timeout=$((SECONDS + ib_timeout))
    while [ "$SECONDS" -lt "$timeout" ]; do
        for ib_dev in "${ib_devs[@]}"; do
            ip link set up "$ib_dev" || true
        done
        sleep 2
        working_ib=0
        good_ibs=()
        for ib_dev in "${ib_devs[@]}"; do
            if ip addr show "$ib_dev" | grep "inet "; then
                good_ibs+=("$ib_dev")
                ((working_ib++)) || true
            fi
            # With udev rules, the ib adapter name has the numa
            # affinity in its name.  On a single adapter system
            # we do not have an easy way to know what that
            # adapter name is in the case of a udev rule, so we have to try
            # both possible names.
            if [ "$working_ib" -ge "$ib_count" ]; then
                return 0
            fi
        done
        sleep ${retry_wait}
    done
    return 1
}

# Get list of actual InfiniBand devices from /sys/class/net/
ib_list=()
for iface in /sys/class/net/ib*; do
    if [ -e "$iface" ]; then
        iface_name=$(basename "$iface")
        ib_list+=("$iface_name")
    fi
done

function check_ib_devices {
    local ib_devs=("$@")
    for iface in "${ib_devs[@]}"; do
        ((testruns++)) || true
        testcases+="  <testcase name=\"Infiniband $iface Working Node $mynodenum\">${nl}"
        set +x
        if ! ip addr show "$iface" | grep "inet "; then
            ib_message="$({
                echo "Found interface $iface with no ip address after reboot on $HOSTNAME."
                ip addr show "$iface" || true
                cat /sys/class/net/"$iface"/mode || true
                ip link set up "$iface" || true
                } 2>&1)"
            mail_message+="${nl}${ib_message}${nl}"
            echo "$ib_message"
            ((testfails++)) || true
            testcases+="    <error message=\"$iface down\" type=\"error\">
      <![CDATA[ $ib_message ]]>
    </error>$nl"
            result=1
        else
            echo "OK: Interface $iface is up."
        fi
        if [ -e "/sys/class/net/$iface/device/numa_node" ]; then
            set -x
            cat "/sys/class/net/$iface/device/numa_node"
        fi
        set -x
        testcases+="  </testcase>$nl"
    done
}

function log_nvme_baseline {
    local device

    echo "NVMe baseline for $HOSTNAME before DAOS tests:"
    echo "# lspci NVMe/VMD devices"
    lspci -Dnnk | grep -Ei 'Non-Volatile|NVMe|VMD|Kernel driver' || true
    echo "# NVMe PCI device driver and IOMMU links"
    while IFS= read -r device; do
        echo "## $device"
        lspci -Dnnk -s "$device" || true
        readlink -f "/sys/bus/pci/devices/$device/driver" 2>&1 || true
        readlink -f "/sys/bus/pci/devices/$device/iommu_group" 2>&1 || true
    done < <(lspci -Dnn | awk '/Non-Volatile memory controller/ {print $1}')
    echo "# /sys/class/iommu"
    ls -la /sys/class/iommu 2>&1 || true
    echo "# nvme list"
    if command -v nvme >/dev/null; then
        nvme list || true
    fi
}

function check_iommu {
    local iommu_entry

    ((testruns++)) || true
    testcases+="  <testcase name=\"VT-d IOMMU Node $mynodenum\">"
    testcases+="${nl}"
    iommu_entry=$(find /sys/class/iommu -mindepth 1 -maxdepth 1 \
        -print -quit 2>/dev/null || true)
    if [ -z "$iommu_entry" ]; then
        iommu_message="FAIL: No active VT-d/IOMMU instance found"
        iommu_message+=" on $HOSTNAME."
        iommu_message+="$nl$(cat /proc/cmdline)"
        iommu_message+="$nl$(dmesg | grep -Ei 'DMAR|IOMMU|VT-d' || true)"
        mail_message+="$nl$iommu_message$nl"
        testcases+="    <error message=\"VT-d disabled\" type=\"error\">
    <![CDATA[ $iommu_message ]]>
    </error>$nl"
        ((testfails++)) || true
        result=7
    else
        iommu_message="OK: Active VT-d/IOMMU instance found"
        iommu_message+=" on $HOSTNAME: $iommu_entry"
        echo "$iommu_message"
        testcases+="    <system-out>"
        testcases+="<![CDATA[ $iommu_message ]]></system-out>${nl}"
    fi
    testcases+="  </testcase>${nl}"
}

function apply_network_alias_rules {
    local query_script="/var/tmp/query_node_interfaces.sh"
    local alias_csv="/var/tmp/daos_ftest_iface_aliases.csv"
    local rules_file="/etc/udev/rules.d/99-daos-interface-alias.rules"
    local tmp_rules_file
    local iface_data
    local letters="abcdefghijklmnopqrstuvwxyz"
    local net_index=0
    local ib_index=0
    local node_num

    declare -A subnet_label_map
    declare -A subnet_iface_count
    declare -A numa_seen

    if [ ! -f "$query_script" ]; then
        echo "ERROR: Missing $query_script"
        return 1
    fi

    if [ ! -x "$query_script" ]; then
        if ! chmod +x "$query_script"; then
            echo "ERROR: Failed to make $query_script executable"
            return 1
        fi
    fi

    iface_data="$($query_script 2>/dev/null || true)"
    if [ -z "$iface_data" ]; then
        echo "ERROR: Empty interface data from $query_script"
        return 1
    fi

    if ! node_num=$(printf "%03d" "$mynodenum"); then
        node_num="000"
    fi

    while IFS='|' read -r _ip _iface iface_type subnet _numa _mac; do
        [ -n "$iface_type" ] || continue
        [ "$iface_type" = "primary" ] && continue
        [ -n "$subnet" ] || continue
        key="$iface_type|$subnet"
        [ -z "${subnet_label_map[$key]:-}" ] || continue
        if [ "$iface_type" = "private" ]; then
            subnet_label_map[$key]="${letters:$net_index:1}"
            net_index=$((net_index + 1))
        elif [ "$iface_type" = "infiniband" ]; then
            subnet_label_map[$key]="${letters:$ib_index:1}"
            ib_index=$((ib_index + 1))
        fi
    done <<< "$(printf "%s\n" "$iface_data" | sort -t'|' -k3,3 -k4,4)"

    while IFS='|' read -r _ip _iface iface_type subnet _numa _mac; do
        [ -n "$iface_type" ] || continue
        [ "$iface_type" = "primary" ] && continue
        group="$iface_type|$subnet"
        subnet_iface_count[$group]=$(( ${subnet_iface_count[$group]:-0} + 1 ))
    done <<< "$iface_data"

    tmp_rules_file=$(mktemp)
    echo "# Managed by ci/functional/test_main_prep_node.sh" > "$tmp_rules_file"
    echo "# Persistent DAOS interface aliases" >> "$tmp_rules_file"
    echo "node,node_num,ip,iface,type,subnet,numa,mac,alias" > "$alias_csv"

    while IFS='|' read -r ip iface iface_type subnet numa mac; do
        [ -n "$ip" ] || continue
        alias_name=""

        if [ "$iface_type" = "primary" ]; then
            alias_name="e_daos_mgmt"
        else
            key="$iface_type|$subnet"
            letter="${subnet_label_map[$key]:-a}"
            group="$iface_type|$subnet"
            group_count="${subnet_iface_count[$group]:-1}"
            suffix="00"

            if [ "$group_count" -gt 1 ]; then
                numa_digit="$numa"
                if [ -z "$numa_digit" ] || [ "$numa_digit" -lt 0 ]; then
                    numa_digit=0
                fi
                idx_key="$group|$numa_digit"
                idx="${numa_seen[$idx_key]:-0}"
                suffix="${numa_digit}${idx}"
                numa_seen[$idx_key]=$((idx + 1))
            fi

            if [ "$iface_type" = "private" ]; then
                alias_name="e_daos_net_${letter}_${suffix}"
            elif [ "$iface_type" = "infiniband" ]; then
                alias_name="ib_daos_${letter}_${suffix}"
            fi
        fi

        [ -n "$alias_name" ] || continue

        if ! ip -d link show dev "$iface" |
             grep -q " altname $alias_name"; then
            if ! ip link property add dev "$iface" altname "$alias_name"; then
                echo "ERROR: Failed to add altname $alias_name to $iface"
                return 1
            fi
        fi

        echo "${HOSTNAME%%.*},$node_num,$ip,$iface,$iface_type,$subnet,$numa,$mac,$alias_name" \
            >> "$alias_csv"

        if [ -n "$mac" ]; then
            rule="SUBSYSTEM==\"net\", ACTION==\"add\", "
            rule+="ATTR{address}==\"$mac\", NAME=\"$alias_name\""
            echo "$rule" >> "$tmp_rules_file"
        fi
    done <<< "$(printf "%s\n" "$iface_data" |
              sort -t'|' -k3,3 -k4,4 -k5,5n -k6,6)"

    if ! cp "$tmp_rules_file" "$rules_file"; then
        rm -f "$tmp_rules_file"
        echo "ERROR: Failed to install $rules_file"
        return 1
    fi
    rm -f "$tmp_rules_file"

    if ! udevadm control --reload-rules; then
        echo "ERROR: Failed to reload udev rules for interface aliases"
        return 1
    fi

    echo "Updated interface alias rules in $rules_file"
    return 0
}

# First check for InfiniBand devices
if [ "$ib_count" -gt 0 ]; then
    if do_wait_for_ib; then
        echo "Found at least $ib_count working devices on $HOSTNAME"
        # All good, generate Junit report
        check_ib_devices "${good_ibs[@]}"
    else
        # Something wrong, generate Junit report and update e-mail
        check_ib_devices "${ib_list[@]}"
    fi
fi

apply_network_alias_rules

# having -x just makes the console log harder to read.
# set +x
if [ "$ib_count" -ge 2 ] ; then
    if [ "$DAOS_PMEM" -gt 0 ]; then
        # now check for pmem & NVMe drives when multiple ib are present.
        # ipmctl show -dimm should show an even number of drives, all healthy
        dimm_count=$(ipmctl show -dimm | grep Healthy -c)
        if [ "$dimm_count" -eq 0 ] || [ $((dimm_count%2)) -ne 0 ]; then
            # May not be fatal, the PMEM DIMM should be replaced when downtime
            # can be # scheduled for this system.
            dimm_message="FAIL: Wrong number $dimm_count healthy PMEM DIMMs seen"
            dimm_message+=" on $HOSTNAME."

            mail_message+="$nl$dimm_message$nl$(ipmctl show -dimm)$nl"
        else
            echo "OK: Found $dimm_count PMEM DIMMs."
        fi
        # Should have 2 regions 0x0000 and 0x0001, type AppDirect
        dimm_rcount=0
        while IFS= read -r line; do
            if [[ "$line" != *"| AppDirect"*"| Healthy"* ]]; then continue; fi
            ((dimm_rcount++)) || true
        done < <(ipmctl show -region)

        ((testruns++)) || true
        testcases+="  <testcase name=\"PMEM DIMM Count Node $mynodenum\">${nl}"
        if [ "$dimm_rcount" -ne 2 ]; then
            pmem_message="FAIL: Found $dimm_rcount of DIMM PMEM regions, need 2"
            pmem_message+=" on $HOSTNAME."
            pmem_message+="$nl$(ipmctl show -region)"
            mail_message+="$nl$pmem_message$nl"
            ((testfails++)) || true
            testcases+="    <error message=\"Bad Count\" type=\"error\">
    <![CDATA[ $pmem_message ]]>
    </error>$nl"
       result=3
        else
            echo "OK: Found $dimm_rcount DIMM PMEM regions."
        fi
        testcases+="  </testcase>$nl"
    fi
    if [ "$DAOS_NVME" -gt 0 ]; then
        # While this gets more data than needed, it is the same search that
        # DAOS tests do and records it in the console log.
        nvme_devices="$(lspci -vmm -D | grep -E '^(Slot|Class|Device|NUMANode):' |
                        grep -E 'Class:\s+Non-Volatile memory controller' -B 1 -A 2)"
        nvme_count=0
        while IFS= read -r line; do
            if [[ "$line" != *"Class:"*"Non-Volatile memory controller"* ]];then
                continue
            fi
            ((nvme_count++)) || true
        done < <(printf %s "$nvme_devices")

        ((testruns++)) || true
        testcases+="  <testcase name=\"NVMe Count Node $mynodenum\">${nl}"
        if [ $((nvme_count%2)) -ne 0 ]; then
            nvme_message="Fail: Odd number ($nvme_count) of NVMe devices seen."
            mail_message+="$nl$nvme_message$nl$nvme_devices$nl"
            ((testfails++)) || true
            testcases+="    <error message=\"Bad Count\" type=\"error\">
      <![CDATA[ $nvme_message$nl$nvme_devices ]]>
    </error>$nl"
            result=4
        else
            echo "OK: Even number ($nvme_count) of NVMe devices seen."
        fi
        testcases+="  </testcase>$nl"
        check_iommu
        log_nvme_baseline
    fi
    # All storage found by lspci should also be in lsblk report
    lsblk_nvme=$(lsblk | grep nvme -c)
    lsblk_pmem=$(lsblk | grep pmem -c)

    if [ "$DAOS_NVME" -gt 0 ]; then
        ((testruns++)) || true
        testcases+="  <testcase name=\"NVMe lsblk Count Node $mynodenum\">${nl}"
        if [ "$lsblk_nvme" -ne "$nvme_count" ]; then
            lsblk_nvme_msg="Fail: Only $lsblk_nvme of $nvme_count NVMe devices seen"
            lsblk_nvme_msg+=" on $HOSTNAME."
            mail_message+="$nl$lsblk_nvme_msg$nl$(lsblk)$nl"
            ((testfails++)) || true
            testcases+="    <error message=\"Bad Count\" type=\"error\">
      <![CDATA[ $lsblk_nvme_msg ]]>
    </error>$nl"
           result=5
        else
            echo "OK: All $nvme_count NVMe devices are in lsblk report."
        fi
        testcases+="  </testcase>$nl"
    fi
    if [ "$DAOS_PMEM" -gt 0 ]; then
        ((testruns++)) || true
        testcases+="  <testcase name=\"PMEM lsblk Count Node $mynodenum\">${nl}"
        if [ "$lsblk_pmem" -ne "$dimm_rcount" ]; then
            lsblk_pmem_msg="Only $lsblk_pmem of $dimm_rcount PMEM devices seen"
            lsblk_pmem_msg+=" on $HOSTNAME."
            mail_message+="$nl$lsblk_pmem_msg$nl$(lsblk)$nl"
            ((testfails++)) || true
            testcases+="    <error message=\"Bad Count\" type=\"error\">
      <![CDATA[ $lsblk_pmem_msg ]]>
    </error>$nl"
           result=6
        else
            echo "OK: All $dimm_rcount PMEM devices are in lsblk report."
        fi
        testcases+="  </testcase>$nl"
    fi
fi

# Additional information if any check failed
if [ "$result" -ne 0 ]; then
    sys_message="$({
        find /etc -name 'ifcfg-*' -type f -print -exec ls -l {} \; || true
        if command -v nmcli >/dev/null 2>&1; then  # New RHEL
            nmcli connection show || true
        fi
        ip link show|| true
        systemctl status || true
        systemctl --failed || true
        journalctl -n 500 || true
    } 2<&1)"
    mail_message+="$nl$sys_message$nl"
fi

set -x
if [ -n "$FIRST_NODE" ] && ! grep -qs ' /mnt/share ' /proc/mounts; then
    mkdir -p /mnt/share
    # Retry the NFS mount to handle the case where the NFS server on
    # FIRST_NODE has not fully registered its exports yet.
    nfs_mounted=false
    for attempt in $(seq 1 3); do
        if mount "$FIRST_NODE":/export/share /mnt/share; then
            nfs_mounted=true
            break
        fi
        echo "NFS mount attempt $attempt failed, retrying in 5s..."
        sleep 5
    done
    if ! "$nfs_mounted"; then
        echo "ERROR: NFS mount failed after $attempt attempts:"
        echo "  $FIRST_NODE:/export/share -> /mnt/share"
        echo "Exports advertised by $FIRST_NODE:"
        showmount -e "$FIRST_NODE" || true
        echo "DNS/hosts resolution for $FIRST_NODE:"
        getent hosts "$FIRST_NODE" || true
        exit 32
    fi
fi

# The package name defaults to "(root)" unless there is a dot in the
# testsuite name, in which case the package name is the part before
# the last dot in the testsuite name.
pn="Hardware"
tf="failures=\"0\""
te="errors=\"$testfails\""
tc="tests=\"$testruns\""

junit_xml="<testsuite name=\"${pn}.${cn}\" skipped=\"0\" $tf $te $tc>$nl
$testcases</testsuite>$nl"

# Each junit file needs the same name for when they are collected.
echo "$junit_xml" > "./hardware_prep_node_results.xml"

if [ "$testfails" -gt 0 ]; then
    mail_type='failed'
fi
do_mail

if [ "$result" -ne 0 ]; then
    echo "Check failure $result"
fi

exit $result
