#!/bin/bash

# set -x
set -u -e -o pipefail

if [[ "$(id -u)" != "0" ]] ; then
	echo "[ERROR] run-daos_server can only be run as root"
fi

mkdir -p /var/run/daos_server/
chmod 755 /var/run/daos_server/
chown root:root /var/run/daos_server/

mkdir -p /etc/daos/certs/clients
chmod 755 /etc/daos/certs
chmod 700 /etc/daos/certs/clients
tar --extract --xz --directory=/etc/daos/certs --no-same-owner --preserve-permissions --file=/run/secrets/daos_server-certs.txz
mv /etc/daos/certs/agent.crt /etc/daos/certs/clients/agent.crt
chmod 644 /etc/daos/certs/daosCA.crt
chmod 644 /etc/daos/certs/server.crt
chmod 400 /etc/daos/certs/server.key
chmod 644 /etc/daos/certs/clients/agent.crt
chown root:root /etc/daos/certs/daosCA.crt
chown root:root /etc/daos/certs/server.crt
chown root:root /etc/daos/certs/server.key
chown root:root /etc/daos/certs/clients/agent.crt

cd /var/run/daos_server/
exec sudo --user=root --group=root /usr/bin/daos_server "$@"
