#!/bin/bash
#
#  Copyright 2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Query node interfaces and categorize them by type.
# Detects primary interface from SSH_CONNECTION environment variable.
# Output: IP|INTERFACE|TYPE|SUBNET|NUMA|MAC (one per line)
#

# Get primary interface from SSH connection
primary_ip=$(echo "$SSH_CONNECTION" | awk '{print $3}')

# Get all interfaces with IPv4 addresses
ip addr show | awk '
    /^[0-9]+:/ {
        iface = $2
        gsub(/:$/, "", iface)
    }
    /inet / && iface !~ /^lo/ {
        split($2, parts, "/")
        ip = parts[1]
        print ip " " iface
    }
' | while read -r ip iface; do
    # Categorize interface: primary (SSH connection IP),
    # InfiniBand (ARPHRD type 32), or private.
    if [ "$ip" = "$primary_ip" ]; then
        iface_type="primary"
    elif [ "$(cat "/sys/class/net/$iface/type" 2>/dev/null)" = "32" ]; then
        iface_type="infiniband"
    else
        iface_type="private"
    fi

    subnet=$(ip -4 route show dev "$iface" proto kernel scope link | \
        awk -v ip="$ip" '$0 ~ ("src " ip) {print $1; exit}')
    numa=$(cat "/sys/class/net/$iface/device/numa_node" 2>/dev/null || echo -1)
    mac=$(cat "/sys/class/net/$iface/address" 2>/dev/null || echo "")

    echo "$ip|$iface|$iface_type|$subnet|$numa|$mac"
done
