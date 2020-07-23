#!/bin/bash

set -eux

for i in 0 1; do
    if [ -e /sys/class/net/ib$i ]; then
        if ! ifconfig ib$i | grep "inet "; then
          {
            echo "Found interface ib$i down after reboot on $HOSTNAME"
            systemctl status
            systemctl --failed
            journalctl -n 500
            ifconfig ib$i
            cat /sys/class/net/ib$i/mode
            ifup ib$i
          } | mail -s "Interface found down after reboot" "$OPERATIONS_EMAIL"
        fi
    fi
done

if ! grep /mnt/share /proc/mounts; then
    mkdir -p /mnt/share
    mount "$FIRST_NODE":/export/share /mnt/share
fi
