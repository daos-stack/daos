#!/bin/bash

# set -x
set -u -e -o pipefail

if [[ $(id -u) -ne 0 ]] ; then
	echo "[ERROR] Could only be used by root"
fi

sed -i -E -e "/DefaultLimitNOFILE/s/.*/DefaultLimitNOFILE=infinity:infinity/" /etc/systemd/system.conf

cat > /etc/security/limits.d/daos-nofile.conf << EOF
# /etc/security/limits.d/daos-nofile.conf

# <domain>	<type>	<item>	<value>
*		hard	nofile	unlimited
*		soft	nofile	unlimited
EOF

systemctl daemon-reexec
