#!/bin/bash
#
#  Copyright 2020-2023 Intel Corporation.
#  Copyright 2025 Hewlett Packard Enterprise Development LP
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

result=0
mail_message=''
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
    mail -s "Hardware check failed after reboot!" \
         -r "$DAOS_DEVOPS_EMAIL" "$OPERATIONS_EMAIL" \
         <<< "$build_info$mail_message"
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
set -x

# Wait for at least the expected IB devices to show up.
# in the case of dual port HBAs, not all IB devices will
# show up.
# For some unknown reason, sometimes IB devices will not show up
# except in the lspci output unless an ip link set up command for
# at least one device that should be present shows up.
good_ibs=()
function do_wait_for_ib {
    local ib_devs=("$@")
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

# Migrating to using udev rules for network devices
if [ -e /etc/udev/rules.d/70-persistent-ipoib.rules ]; then
    ib_list=('ib_cpu0_0' 'ib_cpu1_0')
else
    ib_list=('ib0')
    if [ "$ib_count" -gt 1 ]; then
        ib_list+=('ib1')
    fi
fi

function check_ib_devices {
    local ib_devs=("$@")
    for iface in "${ib_devs[@]}"; do
        ((testruns++)) || true
        testcases+="  <testcase name=\"Infiniband $iface Working Node $mynodenum\">${nl}"
        set +x
        if ! ip addr show "$iface" | grep "inet "; then
            ib_message="$({
                echo "Found interface $iface down after reboot on $HOSTNAME."
                ip addr show "$iface" || true
                cat /sys/class/net/"$iface"/mode || true
                ip link set up "$iface" || true
                cat /etc/sysconfig/network-scripts/ifcfg-"$iface" || true
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


# First check for InfiniBand devices
if [ "$ib_count" -gt 0 ]; then
    if do_wait_for_ib "${ib_list[@]}"; then
        echo "Found at least $ib_count working devices in" "${ib_list[@]}"
        # All good, generate Junit report
        check_ib_devices "${good_ibs[@]}"
    else
        # Something wrong, generate Junit report and update e-mail
        check_ib_devices "${ib_list[@]}"
    fi
fi

# having -x just makes the console log harder to read.
# set +x
if [ "$ib_count" -ge 2 ]; then
    # now check for pmem & NVMe drives when multiple ib are present.
    # ipmctl show -dimm should show an even number of drives, all healthy
    dimm_count=$(ipmctl show -dimm | grep Healthy -c)
    if [ "$dimm_count" -eq 0 ] || [ $((dimm_count%2)) -ne 0 ]; then
       # May not be fatal, the PMEM DIMM should be replaced when downtime can be
       # scheduled for this system.
       dimm_message="FAIL: Wrong number $dimm_count healthy PMEM DIMMs seen."
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
       nvme_message="FAIL: Found $dimm_rcount of DIMM PMEM regions, need 2."
       nvme_message+="$nl$(ipmctl show -region)"
       mail_message+="$nl$nvme_message$nl"
        ((testfails++)) || true
        testcases+="    <error message=\"Bad Count\" type=\"error\">
      <![CDATA[ $nvme_message ]]>
    </error>$nl"
       result=3
    else
       echo "OK: Found $dimm_rcount DIMM PMEM regions."
    fi
    testcases+="  </testcase>$nl"

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

    # All storage found by lspci should also be in lsblk report
    lsblk_nvme=$(lsblk | grep nvme -c)
    lsblk_pmem=$(lsblk | grep pmem -c)

    ((testruns++)) || true
    testcases+="  <testcase name=\"NVMe lsblk Count Node $mynodenum\">${nl}"
    if [ "$lsblk_nvme" -ne "$nvme_count" ]; then
       lsblk_nvme_msg="Fail: Only $lsblk_nvme of $nvme_count NVMe devices seen."
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

    ((testruns++)) || true
    testcases+="  <testcase name=\"PMEM lsblk Count Node $mynodenum\">${nl}"
    if [ "$lsblk_pmem" -ne "$dimm_rcount" ]; then
       lsblk_pmem_msg="Only $lsblk_pmem of $dimm_rcount PMEM devices seen."
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

# Defaulting the package to "(root)" for now as then Jenkins
# will default to setting putting the outer stage name and
# inner stage name in the full test name.
ts="Hardware"
tf="failures=\"$testfails\""
te="errors=\"0\""
tc="tests=\"$testruns\""

# shellcheck disable=SC2089
junit_xml="<testsuite name=\"$ts\" skipped=\"0\" $tf $te $tc>$nl
$testcases</testsuite>$nl"

# Each junit file needs the same name for when they are collected.
echo "$junit_xml" > "./hardware_prep_node_results.xml"

do_mail

if [ "$result" -ne 0 ]; then
    echo "Check failure $result"
fi

exit $result
