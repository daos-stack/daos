#!/bin/bash

# set -x
set -euo pipefail

if [[ $(id -u) -ne 0 ]] ; then
	echo "[ERROR] Could only be used by root"
	exit 1
fi

if [[ -z ${1:+x} ]] ; then
	echo "[ERROR] Number of hugepages was not provided as an argument."
	exit 1
fi

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

if [[ ${1} -eq 0 ]] ; then
	# Needs to reserve some hugepages for the DAOS Xstream targets and system
	# - 512 * 2MB hugepages per target + 512 * 2MB hugepages per sys Xstream (only needed for MD-on-SSD)
	cores_per_socket=$(lscpu --json | jq '.lscpu[] | select(.field=="Core(s) per socket:") | .data | tonumber')
	sockets=$(lscpu --json | jq '.lscpu[] | select(.field=="Socket(s):") | .data | tonumber')
	cores=$((cores_per_socket * sockets))
	hugepages=$((cores * 512))
else
	hugepages="$1"
fi
cat <<< "vm.nr_hugepages = $hugepages" > /etc/sysctl.d/50-hugepages.conf
sysctl --load=/etc/sysctl.d/50-hugepages.conf
