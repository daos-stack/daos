#!/bin/bash
#
#  Copyright 2020-2023 Intel Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
set -eux

: "${FIRST_NODE:=}"
: "${OPERATIONS_EMAIL:=}"
: "${STAGE_NAME:=Unknown}"
: "${BUILD_URL:=Unknown}"

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
         -r "$HOSTNAME"@intel.com "$OPERATIONS_EMAIL" \
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
if [ "$opa_count" -gt 0 ]; then
    ((ib_count=opa_count)) || true
fi

while IFS= read -r line; do
    mlnx_type="${line##*ConnectX-}"
    mlnx_type="${mlnx_type%]*}"
    if [ "$mlnx_type" -ge 6 ]; then
        ((hdr_count++)) || true
    fi
done < <(lspci -mm | grep "ConnectX")
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
The Onmi-Path adapters will not be used."
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
      if ip addr show "$1" | grep "inet "; then
        return 0
      fi
      sleep ${retry_wait}
    done
    return 1
}

# First check for infinband devices
for i in $(seq 0 $((ib_count-1))); do
    ((testruns++)) || true
    testcases+="  <testcase name=\"Infiniband $i Working Node $mynodenum\">${nl}"
    iface="ib$i"
    if do_wait_for_ib "$iface"; then
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
    else
        ib_message="Failed to bring up interface $iface on $HOSTNAME. "
        mail_message+="${nl}${ib_message}${nl}"
        echo "$ib_message"
        ((testfails++)) || true
        testcases+="    <error message=\"$iface down\" type=\"error\">
      <![CDATA[ $ib_message ]]>
    </error>$nl"
        result=1
    fi
    testcases+="  </testcase>$nl"
done

# having -x just makes the console log harder to read.
set +x
if [ -e /sys/class/net/ib1 ]; then
    # now check for pmem & NVMe drives when ib1 is present.
    # ipmctl show -dimm should show an even number of drives, all healthy
    dimm_count=0
    while IFS= read -r line; do
        if [[ "$line" != *"| Healthy "* ]]; then continue; fi
        ((dimm_count++)) || true
    done < <(ipmctl show -dimm)
    if [ "$dimm_count" -eq 0 ] || [ $((dimm_count%2)) -ne 0 ]; then
       # Not fatal, the PMEM DIMM should be replaced when downtime can be
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
       result=1
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
       result=1
    else
       echo "OK: Even number ($nvme_count) of NVMe devices seen."
    fi
    testcases+="  </testcase>$nl"

    # All storage found by lspci should also be in lsblk report
    lsblk_nvme=0
    lsblk_pmem=0
    while IFS= read -r line; do
        if [[ "$line" = nvme* ]];then
            ((lsblk_nvme++)) || true
        fi
        if [[ "$line" = pmem* ]];then
            ((lsblk_pmem++)) || true
        fi
    done < <(lsblk)

    ((testruns++)) || true
    testcases+="  <testcase name=\"NVMe lsblk Count Node $mynodenum\">${nl}"
    if [ "$lsblk_nvme" -ne "$nvme_count" ]; then
       lsblk_nvme_msg="Fail: Only $lsblk_nvme of $nvme_count NVMe devices seen."
       mail_message+="$nl$lsblk_nvme_msg$nl$(lsblk)$nl"
        ((testfails++)) || true
        testcases+="    <error message=\"Bad Count\" type=\"error\">
      <![CDATA[ $lsblk_nvme_msg ]]>
    </error>$nl"
       result=1
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
       result=1
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

exit $result
