#!/bin/bash

# set -x
set -e -o pipefail

if [[ "$(id -u)" != "0" ]] ; then
	echo "[ERROR] run-daos_agent can only be run as root"
fi

mkdir -p /var/run/daos_agent/
chmod 755 /var/run/daos_agent/
chown daos_agent:daos_agent /var/run/daos_agent/

mkdir -p /etc/daos/certs
chmod 755 /etc/daos/certs
tar --extract --xz --directory=/etc/daos/certs --no-same-owner --preserve-permissions --file=/run/secrets/daos_agent-certs.txz
chmod 0644 /etc/daos/certs/daosCA.crt
chmod 0644 /etc/daos/certs/agent.crt
chmod 0400 /etc/daos/certs/agent.key
chown root:root /etc/daos/certs/daosCA.crt
chown daos_agent:daos_agent /etc/daos/certs/agent.crt
chown daos_agent:daos_agent /etc/daos/certs/agent.key

exec sudo --user=daos_agent --group=daos_agent /usr/bin/daos_agent "$@"
