#!/bin/bash

# set -x
set -u -e -o pipefail

if [[ $(id -u) -ne 0 ]] ; then
	echo "[ERROR] Could only be used by root"
	exit 1
fi

sed -i -E -e "/DefaultLimitMEMLOCK/s/.*/DefaultLimitMEMLOCK=infinity:infinity/" /etc/systemd/system.conf

cat > /etc/security/limits.d/daos-memlock.conf << EOF
# /etc/security/limits.d/daos-memlock.conf

# <domain>	<type>	<item>		<value>
*		hard	memlock		unlimited
*		soft	memlock		unlimited
EOF

cat > /etc/profile.d/daos-memlock.sh  << EOF
# Unnlimited memlock for DAOS users
ulimit -l unlimited
EOF
