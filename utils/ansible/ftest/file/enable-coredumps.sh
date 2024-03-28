#!/bin/bash

# set -x
set -u -e -o pipefail

if [[ $(id -u) -ne 0 ]] ; then
	echo "[ERROR] Could only be used by root"
fi

sed -i -E -e "/DefaultLimitCORE/s/.*/DefaultLimitCORE=infinity:infinity/" /etc/systemd/system.conf

cat > /etc/security/limits.d/daos-coredumps.conf << EOF
# /etc/security/limits.d/daos-coredumps.conf

# <domain>	<type>	<item>	<value>
*		hard	core	unlimited
*		soft	core	unlimited
EOF

cat > /etc/sysctl.d/daos-coredumps.conf << EOF
# /etc/sysctl.d/daos-coredumps.conf

fs.suid_dumpable=1
EOF
sysctl -q -w "fs.suid_dumpable=1"
sysctl -p

cat > /etc/profile.d/daos-coredumps.sh  << EOF
# Enable core dump
ulimit -c unlimited
EOF

mkdir -p /etc/systemd/coredump.conf.d/
cat > /etc/systemd/coredump.conf.d/daos-coredumps.conf << EOF
# /etc/systemd/coredump.conf.d/daos-coredumps.conf

[Coredump]
Storage=external
Compress=yes
ProcessSizeMax=8G
ExternalSizeMax=8G
KeepFree=2G
EOF
systemctl daemon-reexec
