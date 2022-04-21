#!/bin/bash

set -eux

report_junit() {
    local msg="$1"
    local name="$2"
    local msg2="$3"

            msg="Failed to bring up interface $iface on $HOSTNAME after it was found down.
Please file a CORCI ticket."
            echo -e "$msg"
    cat <<EOF > results.xml
  <testcase classname="$HOSTNAME" name="$name" time="0">
    <failure message="$msg2" type="TestFail"><![CDATA[$msg]]></failure>
  </testcase>
EOF
}

for i in 0 1; do
    iface="ib$i"
    if [ -e /sys/class/net/"$iface" ]; then
        if ! ifconfig "$iface" | grep "inet "; then
            mkdir -p /tmp/artifacts
            systemctl status > /tmp/artifacts/systemctl\ status || true
            systemctl --failed > /tmp/artifacts/systemctl\ --failed || true
            journalctl -n 500 > /tmp/artifacts/journalctl\ -n\ 500 || true
            dmesg > /tmp/artifacts/dmesg || true
            ifconfig "$iface" > /tmp/artifacts/ifconfig\ "$iface" || true
            cat /sys/class/net/"$iface"/mode > \
                /tmp/artifacts/cat\ _sys_class_net_"${iface}"_mode || true
            ifup "$iface" > /tmp/artifacts/ifup\ "$iface" || true
            ifconfig -a > /tmp/artifacts/ifconfig\ -a || true
            ls -l /etc/sysconfig/network-scripts/ifcfg-* > \
                /tmp/artifacts/ls\ -l\ _etc_sysconfig_network-scripts_ifcfg-\* || true
            cat /etc/sysconfig/network-scripts/ifcfg-"$iface" > \
                /tmp/artifacts/cat\ _etc_sysconfig_network-scripts_ifcfg-"$iface" || true
            send_mail "Interface $iface found down after reboot"                     \
                      "Found interface $iface down after reboot." "/tmp/artifacts/*"
        fi
        if ! ifconfig "$iface" | grep "inet "; then
            send_mail "Interface couldn't e brought up"                                           \
                      "Failed to bring up interface $iface on $HOSTNAME after it was found down."
            report_junit "Failed to bring up interface $iface on $HOSTNAME after it was found down. 
Please file a CORCI ticket." "IB_interface_down" "Interface $iface on $HOSTNAME is down and won't come up. 
Please see ${STAGE_NAME}/framework/ in the artifacts for more info."
            exit 1
        fi
        # make sure verbs driver is present on all IB interfaces
        mkdir -p /tmp/artifacts
        if ! fi_info -d "$iface" -l | tee /tmp/artifacts/fi_info\ -d\ "$iface"\ -l |
          grep verbs; then
            [ -f /tmp/artifacts/dmesg ] || dmesg > /tmp/artifacts/dmesg || true
            mst status -v > /tmp/artifacts/mst\ status\ -v || true
            ibv_devinfo > /tmp/artifacts/ibv_devinfo || true
            send_mail "Interface $iface found without verbs after reboot"         \
                      "Found interface $iface without verbs driver after reboot."
            report_junit "Interface $iface on $HOSTNAME is missing the verbs driver but we do no know how to rectify this so all we can do is fail this test run. 
Please file a CORCI ticket if you have any suggestions on how to rectify this state in order to prevent failed test runs." \
                         "verbs_driver_test"                                                                               \
                         "Missing verbs driver on $HOSTNAME. 
Please see ${STAGE_NAME}/framework/ in the artifacts for more info."
            exit 1
        fi
    fi
done

if ! grep /mnt/share /proc/mounts; then
    mkdir -p /mnt/share
    mount "$FIRST_NODE":/export/share /mnt/share
fi
