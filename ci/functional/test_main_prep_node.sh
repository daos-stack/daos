#!/bin/bash

set -eux

for i in 0 1; do
    iface="ib$i"
    if [ -e /sys/class/net/"$iface" ]; then
        if ! ifconfig "$iface" | grep "inet "; then
          {
            echo "Found interface $iface down after reboot on $HOSTNAME"
            systemctl status || echo "rc=$?"
            systemctl --failed || echo "rc=$?"
            journalctl -n 500 || echo "rc=$?"
            ifconfig "$iface" || echo "rc=$?"
            cat /sys/class/net/"$iface"/mode || echo "rc=$?"
            ifup "$iface" || echo "rc=$?"
            ifconfig -a || echo "rc=$?"
            ls -l /etc/sysconfig/network-scripts/ifcfg-* || echo "rc=$?"
            cat /etc/sysconfig/network-scripts/ifcfg-"$iface" || echo "rc=$?"
          } 2>&1 | mail -s "Interface $iface found down after reboot" \
                   -r "$HOSTNAME"@intel.com "$OPERATIONS_EMAIL"
        fi
        if ! ifconfig "$iface" | grep "inet "; then
            echo "Failed to bring up interface $iface on $HOSTNAME. " \
            "Please file a CORCI ticket."
            exit 1
        fi
    fi
done

if ! grep /mnt/share /proc/mounts; then
    mkdir -p /mnt/share
    mount "$FIRST_NODE":/export/share /mnt/share
fi
