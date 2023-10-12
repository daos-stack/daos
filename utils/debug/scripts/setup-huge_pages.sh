#!/bin/bash

# set -x
set -euo pipefail

# Disable Transparent Huge Pages (THP) to avoid memory fragmentation
cat > /etc/systemd/system/disable-transparent-huge-pages.service <<EOF
[Unit]
Description=Disable Transparent Huge Pages

[Service]
Type=oneshot
ExecStart=/bin/bash -c "/usr/bin/echo "never" | tee /sys/kernel/mm/transparent_hugepage/enabled"
ExecStart=/bin/bash -c "/usr/bin/echo "never" | tee /sys/kernel/mm/transparent_hugepage/defrag"

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable disable-transparent-huge-pages.service
systemctl start disable-transparent-huge-pages.service
systemctl status disable-transparent-huge-pages.service --no-pager --full || true

# Needs to reserve some hugepages for the DAOS Xstream targets and system
# - 512 * 2MB hugepages per target + 512 * 2MB hugepages per sys Xstream (only needed for MD-on-SSD)
cores_per_socket=$( lscpu --json | jq '.lscpu[] | select(.field=="Core(s) per socket:") | .data | tonumber' )
sockets=$( lscpu --json | jq '.lscpu[] | select(.field=="Socket(s):") | .data | tonumber' )
cores=$(( cores_per_socket * sockets ))
hugepages=$(( cores * 1024 ))

# Remove any previous hugepages settings from GRUB_CMDLINE_LINUX_DEFAULT
sudo sed -E -i \
  -e 's/\bhugepages=[0-9]+\b//g' \
  -e 's/\bhugepagesz=[^ ]+//g' \
  -e 's/\bdefault_hugepagesz=[^ ]+//g' \
  /etc/default/grub

# Clean up extra spaces left by removals
sudo sed -E -i 's/GRUB_CMDLINE_LINUX_DEFAULT="([^"]*)"/GRUB_CMDLINE_LINUX_DEFAULT="\1"/; s/  +/ /g' /etc/default/grub

# Insert new hugepages settings at the beginning of the GRUB_CMDLINE_LINUX_DEFAULT value
sudo sed -E -i \
  "s/^(GRUB_CMDLINE_LINUX_DEFAULT=\")([^\"]*)\"/\1default_hugepagesz=2M hugepagesz=2M hugepages=$hugepages \2\"/" \
  /etc/default/grub

# Update GRUB and prompt for reboot
update-grub

echo "Hugepages set to $hugepages via GRUB. Please reboot for changes to take effect."
