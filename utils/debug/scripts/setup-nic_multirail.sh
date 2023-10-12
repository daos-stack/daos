#!/bin/bash

# set -x
set -eu -o pipefail

CWD="$( realpath "$( dirname "$0" )" )"

cat > /etc/sysctl.d/99-nic-multirail.conf <<EOF
# Multirail settings for IB NICs complying with DAOS requirements

# Allow local packets to be accepted on all interfaces
net.ipv4.conf.all.accept_local = 1
# Send ARP replies on the interface targeted in the ARP request
net.ipv4.conf.all.arp_ignore = 2
# Reverse path filtering mode set to loose mode
net.ipv4.conf.ib0.rp_filter = 2
net.ipv4.conf.ib1.rp_filter = 2
EOF
