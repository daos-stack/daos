#!/bin/bash

# set -x
set -e -o pipefail

if [[ "$(id -u)" != "0" ]] ; then
	echo "[ERROR] daos-bash can only be run as root"
fi

mkdir -p /etc/daos/certs
chmod 755 /etc/daos/certs
tar --extract --xz --directory=/etc/daos/certs --no-same-owner --preserve-permissions --file=/run/secrets/daos_admin-certs.txz
chmod 0644 /etc/daos/certs/daosCA.crt
chmod 0644 /etc/daos/certs/admin.crt
chmod 0400 /etc/daos/certs/admin.key
chown root:root /etc/daos/certs/daosCA.crt
chown root:root /etc/daos/certs/admin.crt
chown root:root /etc/daos/certs/admin.key

exec sudo --user=root --group=root /bin/bash "$@"
